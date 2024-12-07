// clang-format off
#define NOMINMAX
#include <winsock2.h>
#include <Windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#include <ip2string.h>
#include <winternl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "wintun.h"
// clang-format on

#include <fmt/xchar.h>

#include "tun.h"
#include "utils.h"

static WINTUN_CREATE_ADAPTER_FUNC* WintunCreateAdapter;
static WINTUN_CLOSE_ADAPTER_FUNC* WintunCloseAdapter;
static WINTUN_OPEN_ADAPTER_FUNC* WintunOpenAdapter;
static WINTUN_GET_ADAPTER_LUID_FUNC* WintunGetAdapterLUID;
static WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC* WintunGetRunningDriverVersion;
static WINTUN_DELETE_DRIVER_FUNC* WintunDeleteDriver;
static WINTUN_SET_LOGGER_FUNC* WintunSetLogger;
static WINTUN_START_SESSION_FUNC* WintunStartSession;
static WINTUN_END_SESSION_FUNC* WintunEndSession;
static WINTUN_GET_READ_WAIT_EVENT_FUNC* WintunGetReadWaitEvent;
static WINTUN_RECEIVE_PACKET_FUNC* WintunReceivePacket;
static WINTUN_RELEASE_RECEIVE_PACKET_FUNC* WintunReleaseReceivePacket;
static WINTUN_ALLOCATE_SEND_PACKET_FUNC* WintunAllocateSendPacket;
static WINTUN_SEND_PACKET_FUNC* WintunSendPacket;

static HMODULE InitializeWintun(void) {
  HMODULE Wintun = LoadLibraryExW(
      L"wintun.dll", NULL,
      LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!Wintun)
    return NULL;
#define X(Name) ((*(FARPROC*)& Name = GetProcAddress(Wintun, #Name)) == NULL)
  if (X(WintunCreateAdapter) || X(WintunCloseAdapter) || X(WintunOpenAdapter) ||
      X(WintunGetAdapterLUID) || X(WintunGetRunningDriverVersion) ||
      X(WintunDeleteDriver) || X(WintunSetLogger) || X(WintunStartSession) ||
      X(WintunEndSession) || X(WintunGetReadWaitEvent) ||
      X(WintunReceivePacket) || X(WintunReleaseReceivePacket) ||
      X(WintunAllocateSendPacket) || X(WintunSendPacket))
#undef X
  {
    DWORD LastError = GetLastError();
    FreeLibrary(Wintun);
    SetLastError(LastError);
    return NULL;
  }
  return Wintun;
}

static void CALLBACK ConsoleLogger(_In_ WINTUN_LOGGER_LEVEL Level,
                                   _In_ DWORD64 Timestamp,
                                   _In_z_ const WCHAR* LogLine) {
  SYSTEMTIME SystemTime;
  FileTimeToSystemTime((FILETIME*)&Timestamp, &SystemTime);
  WCHAR LevelMarker;
  switch (Level) {
    case WINTUN_LOG_INFO:
      LevelMarker = L'+';
      break;
    case WINTUN_LOG_WARN:
      LevelMarker = L'-';
      break;
    case WINTUN_LOG_ERR:
      LevelMarker = L'!';
      break;
    default:
      return;
  }
  fwprintf(stderr, L"%04u-%02u-%02u %02u:%02u:%02u.%04u [%c] %s\n",
           SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay,
           SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond,
           SystemTime.wMilliseconds, LevelMarker, LogLine);
}

static DWORD64 Now(VOID) {
  LARGE_INTEGER Timestamp;
  NtQuerySystemTime(&Timestamp);
  return Timestamp.QuadPart;
}

static void Log(_In_ WINTUN_LOGGER_LEVEL Level,
                _In_z_ const WCHAR* Format,
                ...) {
  WCHAR LogLine[0x200];
  va_list args;
  va_start(args, Format);
  _vsnwprintf_s(LogLine, _countof(LogLine), _TRUNCATE, Format, args);
  va_end(args);
  ConsoleLogger(Level, Now(), LogLine);
}

static DWORD LogError(_In_z_ const WCHAR* Prefix, _In_ DWORD Error) {
  // LOG_ERROR("Error: {}", Error);
  //  use LOG_ERROR to print Prefix and Error
  Log(WINTUN_LOG_ERR, L"%s: Error Code 0x%08X", Prefix, Error);

  // WCHAR *SystemMessage = NULL, *FormattedMessage = NULL;
  // FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER
  // |
  //                    FORMAT_MESSAGE_MAX_WIDTH_MASK,
  //                NULL, HRESULT_FROM_SETUPAPI(Error),
  //                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
  //                (LPWSTR)&SystemMessage, 0, NULL);
  // FormatMessageW(
  //     FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER |
  //         FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_MAX_WIDTH_MASK,
  //     SystemMessage ? L"%1: %3(Code 0x%2!08X!)" : L"%1: Code 0x%2!08X!", 0,
  //     0, (LPWSTR)&FormattedMessage, 0,
  //     (va_list*)(DWORD_PTR[]){(DWORD_PTR)Prefix, (DWORD_PTR)Error,
  //                             (DWORD_PTR)SystemMessage});
  // if (FormattedMessage)
  //   ConsoleLogger(WINTUN_LOG_ERR, Now(), FormattedMessage);
  // LocalFree(FormattedMessage);

  // LocalFree(SystemMessage);

  return Error;
}

static DWORD LogLastError(_In_z_ const WCHAR* Prefix) {
  DWORD LastError = GetLastError();
  LogError(Prefix, LastError);
  SetLastError(LastError);
  return LastError;
}

static void PrintPacket(_In_ const BYTE* Packet, _In_ DWORD PacketSize) {
  if (PacketSize < 20) {
    Log(WINTUN_LOG_INFO, L"Received packet without room for an IP header");
    return;
  }
  BYTE IpVersion = Packet[0] >> 4, Proto;
  WCHAR Src[46], Dst[46];
  if (IpVersion == 4) {
    RtlIpv4AddressToStringW((struct in_addr*)&Packet[12], Src);
    RtlIpv4AddressToStringW((struct in_addr*)&Packet[16], Dst);
    Proto = Packet[9];
    Packet += 20, PacketSize -= 20;
  } else if (IpVersion == 6 && PacketSize < 40) {
    Log(WINTUN_LOG_INFO, L"Received packet without room for an IP header");
    return;
  } else if (IpVersion == 6) {
    RtlIpv6AddressToStringW((struct in6_addr*)&Packet[8], Src);
    RtlIpv6AddressToStringW((struct in6_addr*)&Packet[24], Dst);
    Proto = Packet[6];
    Packet += 40, PacketSize -= 40;
  } else {
    Log(WINTUN_LOG_INFO, L"Received packet that was not IP");
    return;
  }
  if (Proto == 1 && PacketSize >= 8 && Packet[0] == 0)
    Log(WINTUN_LOG_INFO, L"Received IPv%d ICMP echo reply from %s to %s",
        IpVersion, Src, Dst);
  else
    Log(WINTUN_LOG_INFO,
        L"Received IPv%d proto 0x%x packet from %s to %s with size %d",
        IpVersion, Proto, Src, Dst, PacketSize);
}

static USHORT IPChecksum(_In_reads_bytes_(Len) BYTE* Buffer, _In_ DWORD Len) {
  ULONG Sum = 0;
  for (; Len > 1; Len -= 2, Buffer += 2)
    Sum += *(USHORT*)Buffer;
  if (Len)
    Sum += *Buffer;
  Sum = (Sum >> 16) + (Sum & 0xffff);
  Sum += (Sum >> 16);
  return (USHORT)(~Sum);
}

static void MakeICMP(_Out_writes_bytes_all_(28) BYTE Packet[28]) {
  memset(Packet, 0, 28);
  Packet[0] = 0x45;
  *(USHORT*)&Packet[2] = htons(28);
  Packet[8] = 255;
  Packet[9] = 1;
  *(ULONG*)&Packet[12] =
      htonl((10 << 24) | (6 << 16) | (7 << 8) | (8 << 0)); /* 10.6.7.8 */
  *(ULONG*)&Packet[16] =
      htonl((10 << 24) | (6 << 16) | (7 << 8) | (7 << 0)); /* 10.6.7.7 */
  *(USHORT*)&Packet[10] = IPChecksum(Packet, 20);
  Packet[20] = 8;
  *(USHORT*)&Packet[22] = IPChecksum(&Packet[20], 8);
  Log(WINTUN_LOG_INFO,
      L"Sending IPv4 ICMP echo request to 10.6.7.8 from 10.6.7.7");
}

static DWORD WINAPI ReceivePackets(_Inout_ DWORD_PTR SessionPtr) {
  WINTUN_SESSION_HANDLE Session = (WINTUN_SESSION_HANDLE)SessionPtr;
  HANDLE WaitHandles[] = {WintunGetReadWaitEvent(Session)};

  while (true) {
    DWORD PacketSize;
    BYTE* Packet = WintunReceivePacket(Session, &PacketSize);
    if (Packet) {
      PrintPacket(Packet, PacketSize);
      WintunReleaseReceivePacket(Session, Packet);
    } else {
      DWORD LastError = GetLastError();
      switch (LastError) {
        case ERROR_NO_MORE_ITEMS:
          if (WaitForMultipleObjects(_countof(WaitHandles), WaitHandles, FALSE,
                                     INFINITE) == WAIT_OBJECT_0)
            continue;
          return ERROR_SUCCESS;
        default:
          LogError(L"Packet read failed", LastError);
          return LastError;
      }
    }
  }
  return ERROR_SUCCESS;
}

static DWORD WINAPI SendPackets(_Inout_ DWORD_PTR SessionPtr) {
  WINTUN_SESSION_HANDLE Session = (WINTUN_SESSION_HANDLE)SessionPtr;
  while (true) {
#if 0
        BYTE *Packet = WintunAllocateSendPacket(Session, 28);
        if (Packet)
        {
            MakeICMP(Packet);
            WintunSendPacket(Session, Packet);
        }
        else if (GetLastError() != ERROR_BUFFER_OVERFLOW)
            return LogLastError(L"Packet write failed");
#endif
  }
  return ERROR_SUCCESS;
}

namespace SuperSonic {

static bool quit = false;
class WintunInstance {
 public:
  HMODULE Wintun = nullptr;
  WINTUN_ADAPTER_HANDLE Adapter = nullptr;
  WINTUN_SESSION_HANDLE Session = nullptr;

  ~WintunInstance() {
    quit = true;
    if (Session) {
      printf("Wintun End Session\n");
      WintunEndSession(Session);
    }
    if (Adapter) {
      WintunCloseAdapter(Adapter);
    }
    if (Wintun) {
      FreeLibrary(Wintun);
    }
  }
};
static WintunInstance wt;

Tun::Tun(Config::TunOption& tun_option) {
  LOG_INFO("Hello from TUN");

  wt.Wintun = InitializeWintun();
  if (!wt.Wintun) {
    LogError(L"Failed to initialize Wintun", GetLastError());
    throw std::runtime_error("Failed to initialize Wintun");
  }
  WintunSetLogger(ConsoleLogger);
  Log(WINTUN_LOG_INFO, L"Wintun library loaded");

  DWORD LastError;

  DWORD Version;

  GUID ExampleGuid = {0xdeadbabe,
                      0xcafe,
                      0xbeef,
                      {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}};
  wt.Adapter = WintunCreateAdapter(L"Demo", L"Example", &ExampleGuid);
  if (!wt.Adapter) {
    LastError = GetLastError();
    LogError(L"Failed to create adapter", LastError);
    throw std::runtime_error("Failed to create adapter");
  }

  Version = WintunGetRunningDriverVersion();
  Log(WINTUN_LOG_INFO, L"Wintun v%u.%u loaded", (Version >> 16) & 0xff,
      (Version >> 0) & 0xff);

  MIB_UNICASTIPADDRESS_ROW AddressRow;
  InitializeUnicastIpAddressEntry(&AddressRow);
  WintunGetAdapterLUID(wt.Adapter, &AddressRow.InterfaceLuid);
  AddressRow.Address.Ipv4.sin_family = AF_INET;
  AddressRow.Address.Ipv4.sin_addr.S_un.S_addr =
      htonl((172 << 24) | (18 << 16) | (25 << 8) | (tun_option.ip_suffix << 0));
  LOG_INFO("Bind to address 172.18.25.{}", tun_option.ip_suffix);
  AddressRow.OnLinkPrefixLength = 24; /* This is a /24 network */
  AddressRow.DadState = IpDadStatePreferred;
  LastError = CreateUnicastIpAddressEntry(&AddressRow);
  if (LastError != ERROR_SUCCESS && LastError != ERROR_OBJECT_ALREADY_EXISTS) {
    LogError(L"Failed to set IP address", LastError);
    throw std::runtime_error("Failed to set IP address");
  }


  MIB_IPINTERFACE_ROW ipRow = {0};
  InitializeIpInterfaceEntry(&ipRow);
  WintunGetAdapterLUID(wt.Adapter, &ipRow.InterfaceLuid);
  ipRow.Family = AF_INET;
  ipRow.NlMtu = 576;
  DWORD ret = SetIpInterfaceEntry(&ipRow);
  if (ret == NO_ERROR) {
    LOG_INFO("Set device MTU to 576");
  } else {
    LOG_ERROR("Failed to set MTU Error={}", ret);
  }

  // get MTU
  MIB_IF_ROW2 ifRow = {0};
  WintunGetAdapterLUID(wt.Adapter, &ifRow.InterfaceLuid);
  GetIfEntry2(&ifRow);
  LOG_INFO("Device MTU {}", ifRow.Mtu);

  wt.Session = WintunStartSession(wt.Adapter, 0x400000);
  if (!wt.Session) {
    LastError = LogLastError(L"Failed to create adapter");
    throw std::runtime_error("Failed to create adapter");
  }

  LOG_INFO("Successfully initialized TUN");
}

static bool FilterPacket(_In_ const BYTE* Packet, _In_ DWORD PacketSize) {
  // Filter out packets that are not IPv4
  // Filter out packets that destination is not beginning with 172
  if (PacketSize < 20) {
    return false;
  }
  BYTE IpVersion = Packet[0] >> 4;
  if (IpVersion != 4) {
    // LOG_INFO("IP Version = {} != 4", IpVersion);
    return false;
  }
  if (Packet[16] != 172) {
    return false;
  }
  if (Packet[19] == 255) {
    return false;
  }
  return true;
}

void Tun::ReceivePackets() {
  if (wt.Session == nullptr) {
    LOG_ERROR("Session is null");
    throw std::runtime_error("Session is null");
  }
  WINTUN_SESSION_HANDLE Session = wt.Session;
  HANDLE WaitHandles[] = {WintunGetReadWaitEvent(Session)};

  while (!quit) {
    DWORD PacketSize;
    BYTE* Packet = WintunReceivePacket(Session, &PacketSize);
    if (Packet) {
      if (FilterPacket(Packet, PacketSize)) {
        PrintPacket(Packet, PacketSize);
        ByteView bytes(Packet, Packet + PacketSize);
        auto bits = bytesToBits(bytes);
        TxFromSystem->async_send({}, std::move(bits),
                                 [](boost::system::error_code ec) {});
      }
      WintunReleaseReceivePacket(Session, Packet);
    } else {
      DWORD LastError = GetLastError();
      switch (LastError) {
        case ERROR_NO_MORE_ITEMS:
          if (WaitForMultipleObjects(_countof(WaitHandles), WaitHandles, FALSE,
                                     INFINITE) == WAIT_OBJECT_0)
            continue;

          LOG_ERROR("ReceivePackets exited");
          return;

        default:
          LogError(L"Packet read failed", LastError);

          LOG_ERROR("ReceivePackets exited");
          return;
      }
    }
  }

  LOG_ERROR("ReceivePackets exited");
  return;
}

void Tun::rx(BitView bits) {
  if (wt.Session == nullptr) {
    LOG_ERROR("Session is null");
    throw std::runtime_error("Session is null");
  }
  WINTUN_SESSION_HANDLE Session = wt.Session;

  if (bits.size() % 8 != 0) {
    LOG_WARN("Bits size={} is not multiple of 8, will truncate", bits.size());
  }
  auto bytes = bitsToBytes(bits);
  LOG_INFO("Send {} bytes received packet to system", bytes.size());
  BYTE* Packet = WintunAllocateSendPacket(Session, bytes.size());
  if (Packet) {
    std::copy(bytes.begin(), bytes.end(), Packet);
    WintunSendPacket(Session, Packet);
  } else if (GetLastError() != ERROR_BUFFER_OVERFLOW) {
    LogLastError(L"Packet write failed");
  }
}

awaitable<void> Tun::init() {
  auto ex = co_await this_coro::executor;
  TxFromSystem = std::make_unique<Channel>(ex, 10);  // Buffer 10 TX packets

  std::jthread t([&]() { this->ReceivePackets(); });
  t.detach();
}

};  // namespace SuperSonic