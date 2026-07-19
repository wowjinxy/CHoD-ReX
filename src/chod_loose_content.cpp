#include "chod_loose_content.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <rex/filesystem/vfs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/string.h>
#include <rex/string/utf8.h>
#include <rex/system/guest_path.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xmodule.h>
#include <rex/system/xtypes.h>

namespace rex::kernel::xam {
u32 XamContentCreateEnumerator_entry(u32 user_index, u32 device_id, u32 content_type,
                                     u32 content_flags, u32 items_per_enumerate,
                                     mapped_u32 buffer_size_ptr, mapped_u32 handle_out);
u32 XamContentCreateEx_entry(u32 user_index, mapped_string root_name,
                             mapped_void content_data_ptr, u32 flags,
                             mapped_u32 disposition_ptr, mapped_u32 license_mask_ptr,
                             u32 cache_size, u64 content_size, mapped_void overlapped_ptr);
u32 XamContentClose_entry(mapped_string root_name, mapped_void overlapped_ptr);
}  // namespace rex::kernel::xam

REX_EXTERN(__imp__sub_8257E878);
REX_EXTERN(__imp__sub_8257F710);
REX_EXTERN(__imp__sub_82454040);
REX_EXTERN(__imp__sub_824541E8);
REX_EXTERN(__imp__sub_82580878);
REX_EXTERN(__imp__sub_82639E18);
REX_EXTERN(__imp__sub_8263A878);
REX_EXTERN(__imp__sub_8246CBA8);
REX_EXTERN(__imp__sub_82249368);
REX_EXTERN(__imp__sub_825DD4C8);
REX_EXTERN(__imp__sub_825DB208);
REX_EXTERN(__imp__sub_825E02A0);
REX_EXTERN(__imp__sub_82245760);
REX_EXTERN(__imp__XexLoadImage);
REX_EXTERN(ChodResourceObjectAllocTrace);
REX_EXTERN(ChodHeapTreeAllocTrace);
REX_EXTERN(ChodBackendAllocTrace);
REX_EXTERN(ChodHeapAllocWrapperTrace);
REX_EXTERN(ChodGameAllocCallbackTrace);
REX_EXTERN(ChodStageEnemyLegacyListTrace);
REX_EXTERN(ChodEnemyResourceListGetter);
REX_EXTERN(ChodEnemySlot68Getter);
REX_EXTERN(ChodEnemySlot496Setter);
REX_EXTERN(ChodEnemySlot500Setter);
REX_EXTERN(ChodEnemySlot504Setter);
REX_EXTERN(ChodEnemySlot508Setter);
REX_EXTERN(ChodEnemySlot528Setter);
REX_EXTERN(ChodEnemySlot532Setter);
REX_EXTERN(ChodEnemySlot536Setter);
REX_EXTERN(ChodEnemySlot540Setter);
REX_EXTERN(ChodEnemySlot784NameGetter);
REX_EXTERN(ChodEnemySlot808ActiveGetter);
REX_EXTERN(ChodEnemyPsBeelSlot784NameGetter);
REX_EXTERN(ChodEnemyPsBeelSlot808ActiveGetter);
REX_EXTERN(ChodEnemySlot832CountGetter);
REX_EXTERN(ChodEnemyFirstListU32Offset12Getter);
REX_EXTERN(ChodEnemyFirstListU32Offset32Getter);
REX_EXTERN(ChodEnemyFirstListU32Offset36Getter);
REX_EXTERN(ChodEnemyFirstListU32Offset48Getter);
REX_EXTERN(ChodEnemyFirstListU8Offset29Getter);
REX_EXTERN(ChodEnemyFirstListU8Offset30Getter);
REX_EXTERN(ChodEnemyFirstListU8Offset31Getter);
REX_EXTERN(ChodEnemyFirstListU8Offset40Getter);
REX_EXTERN(ChodEnemyFirstListU8Offset41Getter);
REX_EXTERN(ChodEnemySecondListU32Offset124Setter);
REX_EXTERN(ChodEnemySecondListU32Offset128Setter);
REX_EXTERN(ChodEnemyPsLegacyVector220Getter);
REX_EXTERN(ChodEnemyPsLegacyVector236Getter);
REX_EXTERN(ChodEnemyPsLegacyVector252Getter);
REX_EXTERN(ChodEnemyPsLegacyVector268Getter);
REX_EXTERN(ChodEnemyPsOldStageVector40Getter);
REX_EXTERN(ChodEnemyPsOldStageVector56Getter);
REX_EXTERN(ChodEnemyPsOldStageVector56SecondGetter);
REX_EXTERN(ChodEnemyPsOldStageVector52Getter);
REX_EXTERN(ChodEnemyPsOldStageVector100Getter);
REX_EXTERN(ChodPsModernOldSlot412Vector40);
REX_EXTERN(ChodPsModernOldSlot416Vector56);
REX_EXTERN(ChodPsModernOldSlot420Vector56);
REX_EXTERN(ChodPsModernOldSlot424Vector52);
REX_EXTERN(ChodPsModernOldSlot428Vector100);

namespace {

using rex::system::make_object;
using rex::system::XStaticEnumerator;
using rex::system::xam::DummyDeviceId;
using rex::system::xam::GetDummyDeviceInfo;
using rex::system::xam::XCONTENT_AGGREGATE_DATA;
using rex::system::xam::XCONTENT_DATA;
using rex::X_HRESULT;
using rex::X_RESULT;

constexpr uint32_t kMarketplaceContent = 2;
constexpr uint32_t kLooseContentLicenseMask = 1;
constexpr uint32_t kPackageFlagPlayer = 0x1;
constexpr uint32_t kPackageFlagStage = 0x2;
constexpr uint32_t kPackageFlagEnemy = 0x4;
constexpr uint32_t kPackageFlagShot = 0x8;
constexpr uint32_t kPackageFlagGimmick = 0x10;
constexpr uint32_t kPackageFlagItem = 0x200;
constexpr uint32_t kPackageFlagBgm = 0x20;
constexpr uint32_t kPackageFlagText = 0x80;
constexpr uint32_t kPackageFlagInfo = 0x100;
constexpr uint32_t kPackageFlagCredit = 0x400;
constexpr uint32_t kLoosePackageFamilyMask =
    kPackageFlagPlayer | kPackageFlagStage | kPackageFlagEnemy | kPackageFlagShot |
    kPackageFlagGimmick | kPackageFlagItem | kPackageFlagBgm | kPackageFlagText |
    kPackageFlagInfo | kPackageFlagCredit;
constexpr uint32_t kGameHeapGlobals = 0x828D5638;
constexpr uint32_t kGameHeapOffset = 44;
constexpr uint32_t kPackageFileVectorBeginOffset = 20;
constexpr uint32_t kPackageFileVectorEndOffset = 24;
constexpr uint32_t kPackageFileVectorCapacityOffset = 28;
constexpr uint32_t kGameStringSize = 28;
constexpr uint32_t kGameStringInlineCapacity = 15;
constexpr uint32_t kLoaderDataImageBaseOffset = 0x1C;
constexpr uint32_t kLoaderDataImageSizeOffset = 0x20;
constexpr uint32_t kLoaderDataFullImageSizeOffset = 0x38;
constexpr uint32_t kLoaderDataEntryPointOffset = 0x3C;
constexpr uint32_t kLoaderDataLoadCountOffset = 0x40;
constexpr uint32_t kResourceObjectAllocUserSize = 0xBC;
constexpr uint32_t kResourceObjectAllocTotalSize = 0xD0;
constexpr uint32_t kBackendAllocHeaderSize = 8;
constexpr uint32_t kStageLegacyEnemyListOwnerOffset = 0xF3E4;
constexpr std::string_view kGameDataMount = "\\Device\\Harddisk0\\Partition1";
constexpr bool kEnableAllocatorDiagnostics = false;

std::vector<uint32_t> g_loose_package_ids;
std::filesystem::path g_game_data_root;
struct LooseMount {
  std::string root;
  uint32_t package_id;
};
std::mutex g_loose_content_mutex;
std::vector<LooseMount> g_mounted_loose_roots;
std::optional<uint32_t> g_active_loose_package_id;
thread_local uint32_t g_traced_heap_alloc_caller = 0;
thread_local uint32_t g_traced_heap_alloc_request = 0;
thread_local uint32_t g_traced_heap_alloc_align = 0;
thread_local uint32_t g_traced_heap_alloc_depth = 0;
thread_local std::string g_traced_resource_object_name;
thread_local uint32_t g_traced_resource_object_arg5 = 0;
std::atomic<uint32_t> g_empty_legacy_vector = 0;

struct PsOldStageVectorCompatOriginal {
  uint32_t address = 0;
  PPCFunc* original = nullptr;
  std::string_view file_name;
};

std::vector<PsOldStageVectorCompatOriginal> g_ps_old_stage_vector_originals;

struct EnemyResourceGetterPatch {
  std::string_view file_name;
  uint32_t getter_address;
  uint32_t slot_496_pair_setter_address;
  uint32_t slot_500_pair_setter_address;
  uint32_t slot_504_getter_out_address;
  uint32_t slot_508_getter_ret_address;
};

constexpr EnemyResourceGetterPatch kEnemyResourceGetterPatches[] = {
    {"dll_fcaltair.dll", 0x889900A8u, 0x889904E8u, 0x88990500u, 0x88990520u, 0x88990518u},
    {"dll_fcaxe.dll", 0x880900A8u, 0x880904E8u, 0x88090500u, 0x88090520u, 0x88090518u},
    {"dll_fcbat.dll", 0x883100A8u, 0x883104F0u, 0x88310508u, 0x88310528u, 0x88310520u},
    {"dll_fcbigbat.dll", 0x88A900B0u, 0x88A904F0u, 0x88A90508u, 0x88A90528u, 0x88A90520u},
    {"dll_fcbonep.dll", 0x881100A8u, 0x881104F0u, 0x88110508u, 0x88110528u, 0x88110520u},
    {"dll_fccrow.dll", 0x884900A8u, 0x884904E8u, 0x88490500u, 0x88490520u, 0x88490518u},
    {"dll_fcdeath.dll", 0x883900B0u, 0x883904F0u, 0x88390508u, 0x88390528u, 0x88390520u},
    {"dll_fcdracula.dll", 0x8BF100B0u, 0x8BF104F0u, 0x8BF10508u, 0x8BF10528u, 0x8BF10520u},
    {"dll_fcdrapet.dll", 0x88C100A8u, 0x88C104F0u, 0x88C10508u, 0x88C10528u, 0x88C10520u},
    {"dll_fcfishman.dll", 0x889100A8u, 0x889104F0u, 0x88910508u, 0x88910528u, 0x88910520u},
    {"dll_fcfranc.dll", 0x88A100A8u, 0x88A104E8u, 0x88A10500u, 0x88A10520u, 0x88A10518u},
    {"dll_fcghost.dll", 0x881900A8u, 0x881904F0u, 0x88190508u, 0x88190528u, 0x88190520u},
    {"dll_fcguillotine.dll", 0x88C900A8u, 0x88C904E8u, 0x88C90500u, 0x88C90520u, 0x88C90518u},
    {"dll_fclance.dll", 0x882100B0u, 0x882104F8u, 0x88210510u, 0x88210530u, 0x88210528u},
    {"dll_fcmeduhead.dll", 0x888100A8u, 0x888104F0u, 0x88810508u, 0x88810528u, 0x88810520u},
    {"dll_fcmedusa.dll", 0x88B100A8u, 0x88B104F0u, 0x88B10508u, 0x88B10528u, 0x88B10520u},
    {"dll_fcmummy.dll", 0x88B900B0u, 0x88B904F0u, 0x88B90508u, 0x88B90528u, 0x88B90520u},
    {"dll_fcnomi.dll", 0x888900A8u, 0x888904F0u, 0x88890508u, 0x88890528u, 0x88890520u},
    {"dll_fcpanther.dll", 0x882900A8u, 0x882904F0u, 0x88290508u, 0x88290528u, 0x88290520u},
    {"dll_fcredskl.dll", 0x885900A8u, 0x885904E8u, 0x88590500u, 0x88590520u, 0x88590518u},
    {"dll_fcskl.dll", 0x885100B0u, 0x885104F8u, 0x88510510u, 0x88510530u, 0x88510528u},
    {"dll_fcwhitedra.dll", 0x886100A8u, 0x886104F0u, 0x88610508u, 0x88610528u, 0x88610520u},
    {"dll_fczombi.dll", 0x884100A8u, 0x884104E8u, 0x88410500u, 0x88410520u, 0x88410518u},
    {"dll_fudokugan.dll", 0x891900B0u, 0x891904F8u, 0x89190510u, 0x89190530u, 0x89190528u},
    {"dll_fufugubara.dll", 0x88E900A8u, 0x88E904F0u, 0x88E90508u, 0x88E90528u, 0x88E90520u},
    {"dll_fuhannya.dll", 0x890100B0u, 0x890104F8u, 0x89010510u, 0x89010530u, 0x89010528u},
    {"dll_fuhbsr.dll", 0x893100A8u, 0x893104E8u, 0x89310500u, 0x89310520u, 0x89310518u},
    {"dll_fuhiguruma.dll", 0x891100B0u, 0x891104F8u, 0x89110510u, 0x89110530u, 0x89110528u},
    {"dll_fuhisyouka.dll", 0x88F900A8u, 0x88F904F0u, 0x88F90508u, 0x88F90528u, 0x88F90520u},
    {"dll_fuketsugan.dll", 0x890900A8u, 0x890904F0u, 0x89090508u, 0x89090528u, 0x89090520u},
    {"dll_fukyokotsu.dll", 0x89210140u, 0x89210580u, 0x89210598u, 0x892105B8u, 0x892105B0u},
    {"dll_furyukibi.dll", 0x892900A8u, 0x892904F0u, 0x89290508u, 0x89290528u, 0x89290520u},
    {"dll_fusimon.dll", 0x88F100A8u, 0x88F104F0u, 0x88F10508u, 0x88F10528u, 0x88F10520u},
    {"dll_psanfauglir.dll", 0x88E10198u, 0x88E105E0u, 0x88E105F8u, 0x88E10618u, 0x88E10610u},
    {"dll_psbeel.dll", 0x8BF10198u, 0x8BF105D8u, 0x8BF105F0u, 0x8BF10610u, 0x8BF10608u},
    {"dll_psberrigan.dll", 0x88D10198u, 0x88D105D8u, 0x88D105F0u, 0x88D10610u, 0x88D10608u},
    {"dll_psfishh.dll", 0x88690198u, 0x886905D8u, 0x886905F0u, 0x88690610u, 0x88690608u},
    {"dll_psgaibon.dll", 0x88D90198u, 0x88D905E0u, 0x88D905F8u, 0x88D90618u, 0x88D90610u},
    {"dll_pskillerf.dll", 0x88710198u, 0x887105E0u, 0x887105F8u, 0x88710618u, 0x88710610u},
    {"dll_psukobach.dll", 0x88790198u, 0x887905E0u, 0x887905F8u, 0x88790618u, 0x88790610u},
    {"dll_region.dll", 0x8BF100B0u, 0x8BF104F0u, 0x8BF10508u, 0x8BF10528u, 0x8BF10520u},
    {"dll_ryukotsu.dll", 0x8BF100B0u, 0x8BF104F0u, 0x8BF10508u, 0x8BF10528u, 0x8BF10520u},
};

std::string_view EnemyPatchNameForAddress(uint32_t address) {
  for (const auto& patch : kEnemyResourceGetterPatches) {
    if (address >= patch.getter_address && address < patch.getter_address + 0x2000u) {
      return patch.file_name;
    }
  }
  return "unknown";
}

uint8_t* GuestAddress(uint8_t* base, uint32_t guest_address) {
#if defined(_WIN32)
  const uint32_t offset = guest_address >= 0xE0000000u ? 0x1000u : 0u;
#else
  const uint32_t offset = 0;
#endif
  return base + guest_address + offset;
}

uint32_t LoadGuestU32(uint8_t* base, uint32_t guest_address) {
  return __builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(
      GuestAddress(base, guest_address)));
}

uint16_t LoadGuestU16(uint8_t* base, uint32_t guest_address) {
  return __builtin_bswap16(*reinterpret_cast<volatile uint16_t*>(
      GuestAddress(base, guest_address)));
}

uint8_t LoadGuestU8(uint8_t* base, uint32_t guest_address) {
  return *reinterpret_cast<volatile uint8_t*>(GuestAddress(base, guest_address));
}

void StoreGuestU32(uint8_t* base, uint32_t guest_address, uint32_t value) {
  *reinterpret_cast<volatile uint32_t*>(GuestAddress(base, guest_address)) =
      __builtin_bswap32(value);
}

void StoreGuestU16(uint8_t* base, uint32_t guest_address, uint16_t value) {
  *reinterpret_cast<volatile uint16_t*>(GuestAddress(base, guest_address)) =
      __builtin_bswap16(value);
}

void StoreGuestU8(uint8_t* base, uint32_t guest_address, uint8_t value) {
  *reinterpret_cast<volatile uint8_t*>(GuestAddress(base, guest_address)) = value;
}

std::string GuestCString(uint8_t* base, uint32_t guest_address, size_t max_len = 256) {
  if (guest_address < 0x10000u) {
    return {};
  }

  const auto* text = reinterpret_cast<const char*>(GuestAddress(base, guest_address));
  std::string result;
  result.reserve(std::min<size_t>(max_len, 64));
  for (size_t i = 0; i < max_len; ++i) {
    const char c = text[i];
    if (c == '\0') {
      break;
    }
    if (static_cast<unsigned char>(c) < 0x20 && c != '\t') {
      break;
    }
    result.push_back(c);
  }
  return result;
}

bool ShouldTraceHeapAllocCaller(uint32_t caller) {
  switch (caller) {
    case 0x82232118u:
    case 0x82232258u:
    case 0x82232B68u:
    case 0x82232C60u:
    case 0x822410A8u:
    case 0x8224127Cu:
    case 0x82249398u:
    case 0x82249484u:
    case 0x88E11520u:
    case 0x88E14EECu:
      return true;
    default:
      return false;
  }
}

const char* HeapAllocCallerName(uint32_t caller) {
  switch (caller) {
    case 0x82232118u:
      return "sub_82232000 initial 84-byte resource wrapper";
    case 0x82232258u:
      return "sub_822321E8 alternate 84-byte resource wrapper";
    case 0x82232B68u:
      return "sub_82232B18 72-byte resource vector entry";
    case 0x82232C60u:
      return "sub_82232C30 72-byte resource vector entry";
    case 0x822410A8u:
      return "sub_82240F20 pre-entry 16-byte helper record";
    case 0x8224127Cu:
      return "sub_82240F20 enemy entry 84-byte resource wrapper";
    case 0x82249398u:
      return "sub_82249368 backing 188-byte object";
    case 0x82249484u:
      return "sub_82249450 backing 188-byte object";
    case 0x88E11520u:
      return "PSanfauglir 17644-byte object allocation";
    case 0x88E14EECu:
      return "PSanfauglir first-vector growth";
    default:
      return "untracked";
  }
}

uint32_t RotateLeft32(uint32_t value, uint32_t shift) {
  shift &= 31u;
  return shift == 0 ? value : (value << shift) | (value >> (32u - shift));
}

bool IsLikelyGuestPointer(uint32_t address) {
  return address >= 0x10000000u && (address & 3u) == 0;
}

bool IsLikelyHeapVectorPointer(uint32_t address) {
  return address >= 0x40000000u && address < 0x82000000u && (address & 3u) == 0;
}

bool IsLikelyEnemyLegacyVector(uint32_t begin, uint32_t end, uint32_t capacity,
                               uint32_t entry_size, uint32_t max_entries) {
  if (begin == 0 && end == 0 && capacity == 0) {
    return true;
  }
  if (!IsLikelyHeapVectorPointer(begin) || !IsLikelyHeapVectorPointer(end) ||
      !IsLikelyHeapVectorPointer(capacity)) {
    return false;
  }
  if (end < begin || capacity < end) {
    return false;
  }

  const auto byte_count = end - begin;
  const auto capacity_count = capacity - begin;
  if (entry_size != 0) {
    if ((byte_count % entry_size) != 0 || (capacity_count % entry_size) != 0) {
      return false;
    }
    if ((byte_count / entry_size) > max_entries) {
      return false;
    }
  }

  return byte_count <= 0x20000u && capacity_count <= 0x40000u;
}

void LogEnemyCrashPathVtable(uint8_t* base, uint32_t caller, uint32_t object,
                             std::string_view label) {
  static std::atomic<uint32_t> vtable_logs{0};
  const auto log_index = vtable_logs.fetch_add(1);
  if (log_index >= 16) {
    return;
  }

  uint32_t vtable = 0;
  if (IsLikelyGuestPointer(object)) {
    vtable = LoadGuestU32(base, object);
  }

  const auto read_slot = [&](uint32_t offset) -> uint32_t {
    if (!IsLikelyGuestPointer(vtable)) {
      return 0;
    }
    return LoadGuestU32(base, vtable + offset);
  };

  const auto slot_356 = read_slot(356);
  const auto slot_360 = read_slot(360);
  const auto slot_364 = read_slot(364);
  const auto slot_368 = read_slot(368);
  const auto slot_380 = read_slot(380);
  const auto slot_408 = read_slot(408);
  const auto slot_412 = read_slot(412);
  const auto slot_416 = read_slot(416);
  const auto slot_420 = read_slot(420);
  const auto slot_424 = read_slot(424);
  const auto slot_428 = read_slot(428);

  REXLOG_INFO(
      "HoD enemy DLL: crash-path vtable dump {} caller=0x{:08X} object=0x{:08X} "
      "vtable=0x{:08X} slot356=0x{:08X}({}) slot360=0x{:08X}({}) "
      "slot364=0x{:08X}({}) slot368=0x{:08X}({}) slot380=0x{:08X}({})",
      label, caller, object, vtable, slot_356, EnemyPatchNameForAddress(slot_356),
      slot_360, EnemyPatchNameForAddress(slot_360), slot_364,
      EnemyPatchNameForAddress(slot_364), slot_368, EnemyPatchNameForAddress(slot_368),
      slot_380, EnemyPatchNameForAddress(slot_380));
  REXLOG_INFO(
      "HoD enemy DLL: crash-path vtable dump cont object=0x{:08X} "
      "slot408=0x{:08X}({}) slot412=0x{:08X}({}) slot416=0x{:08X}({}) "
      "slot420=0x{:08X}({}) slot424=0x{:08X}({}) slot428=0x{:08X}({})",
      object, slot_408, EnemyPatchNameForAddress(slot_408), slot_412,
      EnemyPatchNameForAddress(slot_412), slot_416, EnemyPatchNameForAddress(slot_416),
      slot_420, EnemyPatchNameForAddress(slot_420), slot_424,
      EnemyPatchNameForAddress(slot_424), slot_428, EnemyPatchNameForAddress(slot_428));
}

uint32_t GuardEnemyLegacyVector(uint8_t* base, uint32_t caller, uint32_t object,
                                uint32_t vector_offset, uint32_t entry_size,
                                uint32_t max_entries, std::string_view label) {
  if (!IsLikelyGuestPointer(object)) {
    static std::atomic<uint32_t> invalid_object_logs{0};
    const auto log_index = invalid_object_logs.fetch_add(1);
    if (log_index < 64) {
      REXLOG_WARN("HoD enemy DLL: {} invalid vector owner caller=0x{:08X} object=0x{:08X}",
                  label, caller, object);
    }
    return 0;
  }

  const auto vector = object + vector_offset;
  const auto begin = LoadGuestU32(base, vector + 4);
  const auto end = LoadGuestU32(base, vector + 8);
  const auto capacity = LoadGuestU32(base, vector + 12);
  const auto valid =
      IsLikelyEnemyLegacyVector(begin, end, capacity, entry_size, max_entries);
  const auto byte_count = valid && end >= begin ? end - begin : 0u;
  const auto count = entry_size == 0 ? byte_count : byte_count / entry_size;

  static std::atomic<uint32_t> vector_logs{0};
  const auto log_index = vector_logs.fetch_add(1);
  if (!valid || log_index < 96) {
    if (valid) {
      REXLOG_INFO(
          "HoD enemy DLL: {} caller=0x{:08X} object=0x{:08X} vector=0x{:08X} "
          "begin=0x{:08X} end=0x{:08X} capacity=0x{:08X} entry_size={} count={} "
          "valid={}",
          label, caller, object, vector, begin, end, capacity, entry_size, count, valid);
    } else {
      REXLOG_WARN(
          "HoD enemy DLL: {} caller=0x{:08X} object=0x{:08X} vector=0x{:08X} "
          "begin=0x{:08X} end=0x{:08X} capacity=0x{:08X} entry_size={} count={} "
          "valid={}",
          label, caller, object, vector, begin, end, capacity, entry_size, count, valid);
    }
  }

  if (!valid) {
    StoreGuestU32(base, vector + 4, 0);
    StoreGuestU32(base, vector + 8, 0);
    StoreGuestU32(base, vector + 12, 0);
  }

  if (caller == 0x82245828u) {
    LogEnemyCrashPathVtable(base, caller, object, label);
  }

  return vector;
}

struct EnemyEntryVector {
  uint32_t begin = 0;
  uint32_t end = 0;
  uint32_t count = 0;
  bool valid = false;
};

EnemyEntryVector ReadEnemyEntryVector(uint8_t* base, uint32_t object,
                                      uint32_t vector_offset) {
  EnemyEntryVector vector{};
  if (!IsLikelyGuestPointer(object)) {
    return vector;
  }

  vector.begin = LoadGuestU32(base, object + vector_offset);
  vector.end = LoadGuestU32(base, object + vector_offset + 4);
  const auto byte_count =
      IsLikelyHeapVectorPointer(vector.begin) && vector.end >= vector.begin
          ? vector.end - vector.begin
          : 0u;
  vector.valid = byte_count != 0 && (byte_count % 132u) == 0;
  vector.count = vector.valid ? byte_count / 132u : 0u;
  return vector;
}

uint32_t ResolveEnemyEntryCandidate(const EnemyEntryVector& vector, uint32_t index) {
  if (!vector.valid || index >= vector.count) {
    return 0;
  }

  return vector.begin + index * 132u;
}

uint32_t ResolveEnemyEntry(uint8_t* base, uint32_t primary_object,
                           uint32_t alternate_object, uint32_t vector_offset,
                           uint32_t primary_index, uint32_t alternate_index,
                           std::string_view label, uint32_t caller) {
  const auto primary_vector = ReadEnemyEntryVector(base, primary_object, vector_offset);
  if (const auto entry = ResolveEnemyEntryCandidate(primary_vector, primary_index);
      entry != 0) {
    return entry;
  }

  if (primary_index != alternate_index) {
    if (const auto entry = ResolveEnemyEntryCandidate(primary_vector, alternate_index);
        entry != 0) {
      static std::atomic<uint32_t> fallback_logs{0};
      const auto log_index = fallback_logs.fetch_add(1);
      if (log_index < 64) {
        REXLOG_INFO(
            "HoD enemy DLL: {} using r5 index fallback caller=0x{:08X} "
            "object=0x{:08X} r4=0x{:08X} r5=0x{:08X} count={}",
            label, caller, primary_object, primary_index, alternate_index,
            primary_vector.count);
      }
      return entry;
    }
  }

  const auto alternate_vector =
      alternate_object != primary_object
          ? ReadEnemyEntryVector(base, alternate_object, vector_offset)
          : EnemyEntryVector{};
  if (const auto entry = ResolveEnemyEntryCandidate(alternate_vector, alternate_index);
      entry != 0) {
    static std::atomic<uint32_t> fallback_logs{0};
    const auto log_index = fallback_logs.fetch_add(1);
    if (log_index < 64) {
      REXLOG_INFO(
          "HoD enemy DLL: {} using r4 object/r5 index fallback caller=0x{:08X} "
          "r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} count={}",
          label, caller, primary_object, alternate_object, alternate_index,
          alternate_vector.count);
    }
    return entry;
  }

  if (!primary_vector.valid || primary_index >= primary_vector.count) {
    static std::atomic<uint32_t> invalid_logs{0};
    const auto log_index = invalid_logs.fetch_add(1);
    if (log_index < 64) {
      REXLOG_WARN(
          "HoD enemy DLL: {} invalid entry vector caller=0x{:08X} "
          "r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} offset={} "
          "begin=0x{:08X} end=0x{:08X} count={} valid={} "
          "alt_begin=0x{:08X} alt_end=0x{:08X} alt_count={} alt_valid={}",
          label, caller, primary_object, primary_index, alternate_index, vector_offset,
          primary_vector.begin, primary_vector.end, primary_vector.count,
          primary_vector.valid, alternate_vector.begin, alternate_vector.end,
          alternate_vector.count, alternate_vector.valid);
    }
    return 0;
  }

  return 0;
}

uint32_t LoadEnemyEntryU32(uint8_t* base, uint32_t primary_object,
                           uint32_t alternate_object, uint32_t vector_offset,
                           uint32_t primary_index, uint32_t alternate_index,
                           uint32_t value_offset, std::string_view label,
                           uint32_t caller) {
  const auto entry =
      ResolveEnemyEntry(base, primary_object, alternate_object, vector_offset,
                        primary_index, alternate_index, label, caller);
  return entry == 0 ? 0 : LoadGuestU32(base, entry + value_offset);
}

uint32_t LoadEnemyEntryU8(uint8_t* base, uint32_t primary_object,
                          uint32_t alternate_object, uint32_t vector_offset,
                          uint32_t primary_index, uint32_t alternate_index,
                          uint32_t value_offset, std::string_view label,
                          uint32_t caller) {
  const auto entry =
      ResolveEnemyEntry(base, primary_object, alternate_object, vector_offset,
                        primary_index, alternate_index, label, caller);
  return entry == 0 ? 0 : LoadGuestU8(base, entry + value_offset);
}

bool StoreEnemyEntryU32Checked(uint8_t* base, uint32_t object,
                               uint32_t vector_offset, uint32_t index,
                               uint32_t value_offset, uint32_t value,
                               std::string_view label, uint32_t caller) {
  const auto vector = ReadEnemyEntryVector(base, object, vector_offset);
  const auto entry = ResolveEnemyEntryCandidate(vector, index);
  if (entry == 0) {
    static std::atomic<uint32_t> invalid_logs{0};
    const auto log_index = invalid_logs.fetch_add(1);
    if (log_index < 64) {
      REXLOG_WARN(
          "HoD enemy DLL: {} skipped invalid vector caller=0x{:08X} "
          "object=0x{:08X} index={} value=0x{:08X} offset={} "
          "begin=0x{:08X} end=0x{:08X} count={} valid={}",
          label, caller, object, index, value, vector_offset, vector.begin,
          vector.end, vector.count, vector.valid);
    }
    return false;
  }

  StoreGuestU32(base, entry + value_offset, value);
  return true;
}

uint32_t AlignHeapRequest(uint32_t request) {
  if (request < 11u) {
    return 16u;
  }
  return (request + 11u) & 0xFFFFFFF8u;
}

uint32_t NormalizeHeapAlignment(uint32_t alignment) {
  if (alignment <= 8u) {
    return alignment;
  }

  auto normalized = std::max(alignment, 16u);
  if ((normalized & (normalized - 1u)) == 0) {
    return normalized;
  }

  uint32_t rounded = 16u;
  while (rounded < normalized && rounded <= 0x40000000u) {
    rounded <<= 1u;
  }
  return rounded;
}

uint32_t BackendAllocatorRequest(uint32_t wrapper_request, uint32_t alignment) {
  const auto internal_request = wrapper_request + kBackendAllocHeaderSize;
  if (alignment <= 8u) {
    return internal_request;
  }

  return AlignHeapRequest(internal_request) + NormalizeHeapAlignment(alignment) + 12u;
}

uint32_t HeapTreeBinForSize(uint32_t size) {
  auto high = RotateLeft32(size, 24) & 0x00FFFFFFu;
  if (high == 0) {
    return 0;
  }
  if (high > 0xFFFFu) {
    return 31;
  }

  const auto shift_8 = RotateLeft32(high - 0x100u, 16) & 0x8u;
  high <<= shift_8;
  const auto shift_4 = RotateLeft32(high - 0x1000u, 16) & 0x4u;
  high <<= shift_4;
  const auto shifted = high << (RotateLeft32(high - 0x4000u, 16) & 0x2u);
  const auto shift_2 = RotateLeft32(high - 0x4000u, 16) & 0x2u;
  const auto index = (((RotateLeft32(shifted, 17) & 0x1FFFFu) - shift_2 -
                       shift_4 - shift_8) +
                      14u);
  const auto low_bit = (size >> ((index + 7u) & 31u)) & 1u;
  return low_bit + ((index << 1u) & 0xFFFFFFFEu);
}

uint32_t LowestSetBitIndex(uint32_t value) {
  for (uint32_t index = 0; index < 32u; ++index) {
    if ((value & (1u << index)) != 0) {
      return index;
    }
  }
  return 32u;
}

uint32_t HeapTreeFallbackMask(uint32_t tree_map, uint32_t tree_bin) {
  if (tree_bin >= 31u) {
    return 0;
  }
  return tree_map & ((0xFFFFFFFFu << tree_bin) << 1u);
}

struct HeapTreeSelection {
  uint32_t chunk_size = 0;
  uint32_t selected = 0;
  uint32_t best_diff = 0;
  uint32_t dv_diff = 0;
};

HeapTreeSelection SelectHeapTreeChunk(uint8_t* base, uint32_t heap, uint32_t total_size) {
  HeapTreeSelection selection{};
  selection.chunk_size = AlignHeapRequest(total_size);
  if (selection.chunk_size < 0x100u || !IsLikelyGuestPointer(heap)) {
    return selection;
  }

  const auto tree_map = LoadGuestU32(base, heap + 4);
  const auto dv_size = LoadGuestU32(base, heap + 8);
  const auto tree_bin = HeapTreeBinForSize(selection.chunk_size);
  uint32_t best_diff = 0u - selection.chunk_size;
  uint32_t selected = 0;
  uint32_t current = LoadGuestU32(base, heap + (tree_bin + 75u) * 4u);
  uint32_t secondary = 0;
  uint32_t walk_bits =
      tree_bin == 31u ? 0u : selection.chunk_size << (25u - (tree_bin >> 1u));

  for (uint32_t depth = 0; depth < 32u && IsLikelyGuestPointer(current); ++depth) {
    const auto node_size = LoadGuestU32(base, current + 4) & 0xFFFFFFFCu;
    const auto diff = node_size - selection.chunk_size;
    const auto child1 = LoadGuestU32(base, current + 20);
    const auto branch = (walk_bits >> 31u) & 1u;
    const auto next = LoadGuestU32(base, current + 16u + branch * 4u);

    if (diff < best_diff) {
      selected = current;
      best_diff = diff;
      if (diff == 0) {
        break;
      }
    }

    if (child1 != 0 && child1 != next) {
      secondary = child1;
    }
    if (next == 0) {
      current = secondary;
      break;
    }

    current = next;
    walk_bits = (walk_bits << 1u) & 0xFFFFFFFEu;
  }

  if (!IsLikelyGuestPointer(current)) {
    const auto fallback_mask = HeapTreeFallbackMask(tree_map, tree_bin);
    const auto fallback_bin = LowestSetBitIndex(fallback_mask);
    current = fallback_bin < 32u ? LoadGuestU32(base, heap + (fallback_bin + 75u) * 4u) : 0u;
  }

  for (uint32_t depth = 0; depth < 32u && IsLikelyGuestPointer(current); ++depth) {
    const auto node_size = LoadGuestU32(base, current + 4) & 0xFFFFFFFCu;
    const auto diff = node_size - selection.chunk_size;
    const auto child0 = LoadGuestU32(base, current + 16);
    const auto child1 = LoadGuestU32(base, current + 20);

    if (diff < best_diff) {
      selected = current;
      best_diff = diff;
    }
    current = child0 != 0 ? child0 : child1;
  }

  selection.selected = selected;
  selection.best_diff = best_diff;
  selection.dv_diff = dv_size - selection.chunk_size;
  return selection;
}

void RepairHeapTreeSingletonBacklink(uint8_t* base, uint32_t heap, uint32_t total_size,
                                     std::string_view resource_name) {
  const auto selection = SelectHeapTreeChunk(base, heap, total_size);
  if (!IsLikelyGuestPointer(selection.selected) ||
      !(selection.best_diff < selection.dv_diff)) {
    return;
  }

  const auto chunk = selection.selected;
  const auto fd = LoadGuestU32(base, chunk + 8);
  const auto bk = LoadGuestU32(base, chunk + 12);
  const auto child0 = LoadGuestU32(base, chunk + 16);
  const auto child1 = LoadGuestU32(base, chunk + 20);
  const auto parent = LoadGuestU32(base, chunk + 24);
  const auto index = LoadGuestU32(base, chunk + 28);
  const auto expected_root_slot =
      index < 32u ? heap + (index + 75u) * 4u : 0u;
  const auto root_parent_points_to_chunk =
      IsLikelyGuestPointer(parent) && LoadGuestU32(base, parent) == chunk;
  const auto parent_child0 =
      IsLikelyGuestPointer(parent) ? LoadGuestU32(base, parent + 16) : 0u;
  const auto parent_child1 =
      IsLikelyGuestPointer(parent) ? LoadGuestU32(base, parent + 20) : 0u;
  const auto parent_child_points_to_chunk =
      parent_child0 == chunk || parent_child1 == chunk;
  const auto is_root_parent =
      expected_root_slot != 0 && parent == expected_root_slot;
  const auto parent_kind =
      is_root_parent && root_parent_points_to_chunk
          ? "root"
          : parent_child0 == chunk ? "child0" : parent_child1 == chunk ? "child1" : "none";

  if (fd != chunk || child0 != 0 || child1 != 0 ||
      (!(is_root_parent && root_parent_points_to_chunk) &&
       !parent_child_points_to_chunk)) {
    return;
  }

  if (bk == chunk) {
    return;
  }

  StoreGuestU32(base, chunk + 12, chunk);
  static std::atomic<uint32_t> repair_logs{0};
  const auto log_index = repair_logs.fetch_add(1);
  if (log_index < 64) {
    REXLOG_WARN(
        "HoD heap repair: restored singleton tree bk chunk=0x{:08X} old_bk=0x{:08X} "
        "heap=0x{:08X} request=0x{:X} index={} parent=0x{:08X} "
        "parent_kind={} resource='{}'",
        chunk, bk, heap, total_size, index, parent, parent_kind, resource_name);
  }
}

void LogHeapChunkSummary(uint8_t* base, uint32_t heap, uint32_t chunk,
                         std::string_view label) {
  if (!IsLikelyGuestPointer(chunk)) {
    REXLOG_INFO("HoD heap trace: {} chunk=0x{:08X} skipped heap=0x{:08X}",
                label, chunk, heap);
    return;
  }

  const auto size_flags = LoadGuestU32(base, chunk + 4);
  const auto fd = LoadGuestU32(base, chunk + 8);
  const auto bk = LoadGuestU32(base, chunk + 12);
  const auto child0 = LoadGuestU32(base, chunk + 16);
  const auto child1 = LoadGuestU32(base, chunk + 20);
  const auto parent = LoadGuestU32(base, chunk + 24);
  const auto index = LoadGuestU32(base, chunk + 28);
  REXLOG_INFO(
      "HoD heap trace: {} chunk=0x{:08X} size_flags=0x{:08X} fd=0x{:08X} "
      "bk=0x{:08X} child0=0x{:08X} child1=0x{:08X} parent=0x{:08X} index={}",
      label, chunk, size_flags, fd, bk, child0, child1, parent, index);
}

void LogHeapTreeSelection(uint8_t* base, uint32_t heap, uint32_t total_size) {
  const auto chunk_size = AlignHeapRequest(total_size);
  if (chunk_size < 0x100u || !IsLikelyGuestPointer(heap)) {
    return;
  }

  const auto tree_map = LoadGuestU32(base, heap + 4);
  const auto dv_size = LoadGuestU32(base, heap + 8);
  const auto least_addr = LoadGuestU32(base, heap + 16);
  const auto tree_bin = HeapTreeBinForSize(chunk_size);
  const auto exact_root = LoadGuestU32(base, heap + (tree_bin + 75u) * 4u);
  uint32_t best_diff = 0u - chunk_size;
  uint32_t selected = 0;
  uint32_t current = exact_root;
  uint32_t secondary = 0;
  uint32_t walk_bits = tree_bin == 31u ? 0u : chunk_size << (25u - (tree_bin >> 1u));

  REXLOG_INFO(
      "HoD heap trace: tree-select begin heap=0x{:08X} request=0x{:X} "
      "chunk_size=0x{:X} tree_bin={} exact_root=0x{:08X} tree_map=0x{:08X}",
      heap, total_size, chunk_size, tree_bin, exact_root, tree_map);

  for (uint32_t depth = 0; depth < 32u && IsLikelyGuestPointer(current); ++depth) {
    const auto node_size = LoadGuestU32(base, current + 4) & 0xFFFFFFFCu;
    const auto diff = node_size - chunk_size;
    const auto child1 = LoadGuestU32(base, current + 20);
    const auto branch = (walk_bits >> 31u) & 1u;
    const auto next = LoadGuestU32(base, current + 16u + branch * 4u);
    REXLOG_INFO(
        "HoD heap trace: tree-select exact depth={} node=0x{:08X} "
        "node_size=0x{:X} diff=0x{:X} branch={} next=0x{:08X} child1=0x{:08X}",
        depth, current, node_size, diff, branch, next, child1);

    if (diff < best_diff) {
      selected = current;
      best_diff = diff;
      if (diff == 0) {
        break;
      }
    }

    if (child1 != 0 && child1 != next) {
      secondary = child1;
    }
    if (next == 0) {
      current = secondary;
      break;
    }

    current = next;
    walk_bits = (walk_bits << 1u) & 0xFFFFFFFEu;
  }

  if (!IsLikelyGuestPointer(current)) {
    const auto fallback_mask = HeapTreeFallbackMask(tree_map, tree_bin);
    const auto fallback_bin = LowestSetBitIndex(fallback_mask);
    current = fallback_bin < 32u ? LoadGuestU32(base, heap + (fallback_bin + 75u) * 4u) : 0u;
    REXLOG_INFO(
        "HoD heap trace: tree-select fallback mask=0x{:08X} bin={} root=0x{:08X}",
        fallback_mask, fallback_bin, current);
  }

  for (uint32_t depth = 0; depth < 32u && IsLikelyGuestPointer(current); ++depth) {
    const auto node_size = LoadGuestU32(base, current + 4) & 0xFFFFFFFCu;
    const auto diff = node_size - chunk_size;
    const auto child0 = LoadGuestU32(base, current + 16);
    const auto child1 = LoadGuestU32(base, current + 20);
    REXLOG_INFO(
        "HoD heap trace: tree-select sweep depth={} node=0x{:08X} "
        "node_size=0x{:X} diff=0x{:X} child0=0x{:08X} child1=0x{:08X}",
        depth, current, node_size, diff, child0, child1);

    if (diff < best_diff) {
      selected = current;
      best_diff = diff;
    }
    current = child0 != 0 ? child0 : child1;
  }

  if (!IsLikelyGuestPointer(selected)) {
    REXLOG_INFO(
        "HoD heap trace: tree-select selected=0x{:08X} skipped best_diff=0x{:X}",
        selected, best_diff);
    return;
  }

  const auto selected_size = LoadGuestU32(base, selected + 4) & 0xFFFFFFFCu;
  const auto selected_fd = LoadGuestU32(base, selected + 8);
  const auto selected_bk = LoadGuestU32(base, selected + 12);
  const auto selected_child0 = LoadGuestU32(base, selected + 16);
  const auto selected_child1 = LoadGuestU32(base, selected + 20);
  const auto selected_parent = LoadGuestU32(base, selected + 24);
  const auto selected_index = LoadGuestU32(base, selected + 28);
  const auto dv_diff = dv_size - chunk_size;
  REXLOG_INFO(
      "HoD heap trace: tree-select selected=0x{:08X} selected_size=0x{:X} "
      "best_diff=0x{:X} dv_size=0x{:X} dv_diff=0x{:X} least_addr=0x{:08X} "
      "fd=0x{:08X} bk=0x{:08X} child0=0x{:08X} child1=0x{:08X} "
      "parent=0x{:08X} index={} will_unlink={}",
      selected, selected_size, best_diff, dv_size, dv_diff, least_addr, selected_fd,
      selected_bk, selected_child0, selected_child1, selected_parent, selected_index,
      best_diff < dv_diff);
  if (selected_bk == 0 && selected_fd >= least_addr) {
    REXLOG_WARN(
        "HoD heap trace: selected tree chunk has null bk before unlink "
        "selected=0x{:08X} fd=0x{:08X} least_addr=0x{:08X}",
        selected, selected_fd, least_addr);
  }
}

void LogHeapAllocatorSnapshot(uint8_t* base, uint32_t heap, uint32_t total_size) {
  if (!IsLikelyGuestPointer(heap)) {
    REXLOG_INFO("HoD heap trace: invalid heap=0x{:08X} total=0x{:X}", heap,
                total_size);
    return;
  }

  const auto small_map = LoadGuestU32(base, heap + 0);
  const auto tree_map = LoadGuestU32(base, heap + 4);
  const auto dv_size = LoadGuestU32(base, heap + 8);
  const auto top_size = LoadGuestU32(base, heap + 12);
  const auto least_addr = LoadGuestU32(base, heap + 16);
  const auto dv = LoadGuestU32(base, heap + 20);
  const auto top = LoadGuestU32(base, heap + 24);
  const auto chunk_size = AlignHeapRequest(total_size);
  const auto small_index = chunk_size >> 3u;
  const auto small_sentinel = heap + 36u + small_index * 8u;
  const auto small_fd =
      small_index < 32u ? LoadGuestU32(base, small_sentinel + 8u) : 0u;
  const auto small_bk =
      small_index < 32u ? LoadGuestU32(base, small_sentinel + 12u) : 0u;
  const auto tree_bin = HeapTreeBinForSize(chunk_size);
  const auto tree_root_addr = heap + (tree_bin + 75u) * 4u;
  const auto tree_root = tree_map == 0 ? 0 : LoadGuestU32(base, tree_root_addr);
  const auto fallback_mask = HeapTreeFallbackMask(tree_map, tree_bin);
  const auto fallback_bin = LowestSetBitIndex(fallback_mask);
  const auto fallback_root_addr =
      fallback_bin < 32u ? heap + (fallback_bin + 75u) * 4u : 0u;
  const auto fallback_root =
      fallback_bin < 32u ? LoadGuestU32(base, fallback_root_addr) : 0u;
  REXLOG_INFO(
      "HoD heap trace: internal_heap=0x{:08X} request=0x{:X} chunk_size=0x{:X} "
      "small_index={} small_sentinel=0x{:08X} small_fd=0x{:08X} "
      "small_bk=0x{:08X} tree_bin={} tree_root_slot=0x{:08X} tree_root=0x{:08X} "
      "fallback_mask=0x{:08X} fallback_bin={} fallback_root_slot=0x{:08X} "
      "fallback_root=0x{:08X} "
      "small_map=0x{:08X} tree_map=0x{:08X} dv_size=0x{:08X} "
      "top_size=0x{:08X} dv=0x{:08X} top=0x{:08X} least_addr=0x{:08X}",
      heap, total_size, chunk_size, small_index, small_sentinel, small_fd, small_bk,
      tree_bin, tree_root_addr, tree_root, fallback_mask, fallback_bin,
      fallback_root_addr, fallback_root, small_map, tree_map, dv_size, top_size, dv,
      top, least_addr);
  if (small_fd != small_sentinel) {
    LogHeapChunkSummary(base, heap, small_fd, "small-bin fd");
  }
  LogHeapChunkSummary(base, heap, tree_root, "tree-bin root");
  if (fallback_root != 0 && fallback_root != tree_root) {
    LogHeapChunkSummary(base, heap, fallback_root, "fallback tree-bin root");
  }
  LogHeapTreeSelection(base, heap, total_size);
}

void LogAllocatorChainSnapshot(uint8_t* base, uint32_t allocator,
                               uint32_t wrapper_total_size,
                               std::string_view resource_name) {
  if (!IsLikelyGuestPointer(allocator)) {
    REXLOG_INFO("HoD heap trace: invalid allocator wrapper=0x{:08X} resource='{}'",
                allocator, resource_name);
    return;
  }

  const auto allocator_vtable = LoadGuestU32(base, allocator + 0);
  const auto backend = LoadGuestU32(base, allocator + 40);
  const auto backend_vtable = IsLikelyGuestPointer(backend) ? LoadGuestU32(base, backend) : 0u;
  const auto internal_heap =
      IsLikelyGuestPointer(backend) ? LoadGuestU32(base, backend + 40) : 0u;
  const auto internal_request = wrapper_total_size + kBackendAllocHeaderSize;
  REXLOG_INFO(
      "HoD heap trace: allocator chain wrapper=0x{:08X} wrapper_vtable=0x{:08X} "
      "backend=0x{:08X} backend_vtable=0x{:08X} internal_heap=0x{:08X} "
      "wrapper_total=0x{:X} internal_request=0x{:X} resource='{}'",
      allocator, allocator_vtable, backend, backend_vtable, internal_heap,
      wrapper_total_size, internal_request, resource_name);
  LogHeapAllocatorSnapshot(base, internal_heap, internal_request);
}

uint32_t AllocateGuest(PPCContext& ctx, uint8_t* base, uint32_t bytes) {
  if (bytes == 0) {
    return 0;
  }

  const auto heap = LoadGuestU32(base, kGameHeapGlobals + kGameHeapOffset);
  if (heap == 0) {
    REXLOG_WARN("HoD loose content: game heap is not ready for {} byte allocation", bytes);
    return 0;
  }

  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = heap;
  frame.ctx.r4.u64 = bytes;
  frame.ctx.r5.u64 = 0;
  __imp__sub_825E02A0(frame.ctx, base);
  return frame.ctx.r3.u32;
}

uint32_t EnsureEmptyLegacyVector(PPCContext& ctx, uint8_t* base) {
  if (const auto existing = g_empty_legacy_vector.load(std::memory_order_acquire);
      IsLikelyGuestPointer(existing)) {
    return existing;
  }

  const auto allocated = AllocateGuest(ctx, base, 16);
  if (allocated != 0) {
    std::memset(GuestAddress(base, allocated), 0, 16);
    uint32_t expected = 0;
    if (g_empty_legacy_vector.compare_exchange_strong(
            expected, allocated, std::memory_order_release,
            std::memory_order_acquire)) {
      REXLOG_INFO("HoD enemy DLL: allocated shared empty legacy vector at 0x{:08X}",
                  allocated);
      return allocated;
    }
    return g_empty_legacy_vector.load(std::memory_order_acquire);
  }

  const auto stack_scratch = (ctx.r1.u32 + 112u) & ~3u;
  if (IsLikelyGuestPointer(stack_scratch)) {
    std::memset(GuestAddress(base, stack_scratch), 0, 16);
    return stack_scratch;
  }

  return 0;
}

bool IsOldStageVectorCaller(uint32_t caller) {
  return caller >= 0x82246100u && caller < 0x82247480u;
}

bool ReturnEmptyLegacyVectorForOldStageCaller(PPCContext& ctx, uint8_t* base,
                                              uint32_t old_slot_offset,
                                              uint32_t entry_size,
                                              std::string_view label) {
  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (!IsOldStageVectorCaller(caller)) {
    return false;
  }

  const auto empty_vector = EnsureEmptyLegacyVector(ctx, base);
  static std::atomic<uint32_t> old_vector_logs{0};
  const auto log_index = old_vector_logs.fetch_add(1);
  if (log_index < 96) {
    REXLOG_INFO(
        "HoD enemy DLL: {} old stage vector shim caller=0x{:08X} "
        "target=0x{:08X} object=0x{:08X} old_slot=0x{:X} entry_size={} "
        "result=0x{:08X}",
        label, caller, ctx.last_indirect_target, ctx.r3.u32, old_slot_offset,
        entry_size, empty_vector);
  }
  ctx.r3.u64 = empty_vector;
  return true;
}

PPCFunc* FindPsOldStageVectorOriginal(uint32_t address) {
  for (auto& entry : g_ps_old_stage_vector_originals) {
    if (entry.address == address) {
      return entry.original;
    }
  }
  return nullptr;
}

bool RegisterPsOldStageVectorCompatSlot(
    rex::runtime::FunctionDispatcher* dispatcher, std::string_view file_name,
    uint32_t address, PPCFunc* hook) {
  auto* original = dispatcher->GetFunction(address);
  auto existing = std::find_if(
      g_ps_old_stage_vector_originals.begin(), g_ps_old_stage_vector_originals.end(),
      [address](const PsOldStageVectorCompatOriginal& entry) {
        return entry.address == address;
      });

  if (existing == g_ps_old_stage_vector_originals.end()) {
    if (original == hook) {
      REXLOG_WARN(
          "HoD enemy DLL: PS old-stage vector compat missing original for {} "
          "address=0x{:08X}",
          file_name, address);
    }
    g_ps_old_stage_vector_originals.push_back({address, original, file_name});
  } else if (existing->original == nullptr && original != hook) {
    existing->original = original;
    existing->file_name = file_name;
  }

  return dispatcher->SetFunction(address, hook);
}

void HandlePsModernOldSlotVector(PPCContext& ctx, uint8_t* base,
                                 uint32_t old_slot_offset, uint32_t entry_size,
                                 std::string_view label) {
  if (ReturnEmptyLegacyVectorForOldStageCaller(ctx, base, old_slot_offset,
                                               entry_size, label)) {
    return;
  }

  const auto target = ctx.last_indirect_target;
  auto* original = FindPsOldStageVectorOriginal(target);
  if (original != nullptr) {
    original(ctx, base);
    return;
  }

  static std::atomic<uint32_t> missing_original_logs{0};
  const auto log_index = missing_original_logs.fetch_add(1);
  if (log_index < 32) {
    REXLOG_WARN("HoD enemy DLL: {} missing modern original target=0x{:08X} "
                "caller=0x{:08X} object=0x{:08X}",
                label, target, static_cast<uint32_t>(ctx.lr), ctx.r3.u32);
  }
  ctx.r3.u64 = 0;
}

void HandlePsOldStageVectorOrSetOnce(PPCContext& ctx, uint8_t* base,
                                      uint32_t field_offset,
                                      uint32_t old_slot_offset,
                                      uint32_t entry_size,
                                      std::string_view label) {
  if (ReturnEmptyLegacyVectorForOldStageCaller(ctx, base, old_slot_offset,
                                               entry_size, label)) {
    return;
  }

  if (!IsLikelyGuestPointer(ctx.r3.u32)) {
    return;
  }

  const auto field = ctx.r3.u32 + field_offset;
  if (LoadGuestU32(base, field) == 0) {
    StoreGuestU32(base, field, ctx.r4.u32);
  }
}

void ClearGuestMemory(uint8_t* base, uint32_t guest_address, uint32_t bytes) {
  std::memset(GuestAddress(base, guest_address), 0, bytes);
}

void CopyGuestMemory(uint8_t* base, uint32_t guest_address, const void* source, size_t bytes) {
  std::memcpy(GuestAddress(base, guest_address), source, bytes);
}

bool WriteGameString(PPCContext& ctx, uint8_t* base, uint32_t guest_string,
                     std::string_view value) {
  ClearGuestMemory(base, guest_string, kGameStringSize);
  const auto size = static_cast<uint32_t>(value.size());

  if (size <= kGameStringInlineCapacity) {
    if (!value.empty()) {
      CopyGuestMemory(base, guest_string + 4, value.data(), value.size());
    }
    StoreGuestU32(base, guest_string + 20, size);
    StoreGuestU32(base, guest_string + 24, kGameStringInlineCapacity);
    return true;
  }

  const auto text = AllocateGuest(ctx, base, size + 1);
  if (text == 0) {
    return false;
  }

  CopyGuestMemory(base, text, value.data(), value.size());
  StoreGuestU8(base, text + size, 0);
  StoreGuestU32(base, guest_string + 4, text);
  StoreGuestU32(base, guest_string + 20, size);
  StoreGuestU32(base, guest_string + 24, size);
  return true;
}

std::string PackageNumberedFileName(const char* prefix, uint32_t package_id,
                                    const char* suffix) {
  char name[64]{};
  std::snprintf(name, sizeof(name), "%s%02u%s", prefix, package_id, suffix);
  return name;
}

bool HasDlcManifestName(std::string_view path) {
  return path.find("_dlc") != std::string_view::npos ||
         path.find("_DLC") != std::string_view::npos;
}

bool PathExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

char LowerAscii(char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c - 'A' + 'a');
  }
  return c;
}

bool AsciiEqualCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (size_t i = 0; i < lhs.size(); ++i) {
    if (LowerAscii(lhs[i]) != LowerAscii(rhs[i])) {
      return false;
    }
  }
  return true;
}

bool AsciiStartsWithCase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) {
    return false;
  }
  return AsciiEqualCase(text.substr(0, prefix.size()), prefix);
}

bool AsciiEndsWithCase(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size()) {
    return false;
  }
  return AsciiEqualCase(text.substr(text.size() - suffix.size()), suffix);
}

bool IsGuardedLegacyEnemyModule(std::string_view file_name) {
  return AsciiStartsWithCase(file_name, "dll_ps") ||
         AsciiStartsWithCase(file_name, "dll_fu");
}

bool IsPsEnemyModule(std::string_view file_name) {
  return AsciiStartsWithCase(file_name, "dll_ps");
}

bool FileNameMatchesPrefixSuffix(std::string_view name, std::string_view prefix,
                                 std::string_view suffix) {
  return AsciiStartsWithCase(name, prefix) && AsciiEndsWithCase(name, suffix);
}

std::string NormalizeLooseRulePath(std::string_view path) {
  auto normalized = rex::string::utf8_lower_ascii(path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  while (AsciiStartsWithCase(normalized, "data/")) {
    normalized.erase(0, 5);
  }
  return normalized;
}

bool PathStartsWithCase(std::string_view path, std::string_view prefix) {
  return AsciiStartsWithCase(NormalizeLooseRulePath(path), prefix);
}

bool FileStemIsAny(std::string_view file_name,
                   std::initializer_list<std::string_view> stems) {
  auto lower_name = rex::string::utf8_lower_ascii(file_name);
  const auto dot = lower_name.find_last_of('.');
  const auto stem =
      dot == std::string::npos ? std::string_view(lower_name)
                               : std::string_view(lower_name).substr(0, dot);
  for (const auto candidate : stems) {
    if (AsciiEqualCase(stem, candidate)) {
      return true;
    }
  }
  return false;
}

bool AnyFileWithPrefixSuffix(const std::filesystem::path& dir, std::string_view prefix,
                             std::string_view suffix) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return false;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      return false;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto name = rex::path_to_utf8(entry.path().filename());
    if (FileNameMatchesPrefixSuffix(name, prefix, suffix)) {
      return true;
    }
  }
  return false;
}

bool AnyDlcItemTable(uint32_t package_id) {
  if (g_game_data_root.empty()) {
    return false;
  }

  const auto data_root = g_game_data_root / "data";
  const auto needle = PackageNumberedFileName("_dlc", package_id, ".dat");
  std::error_code ec;
  for (const auto& root : {data_root / "item", data_root / "opd"}) {
    if (!std::filesystem::is_directory(root, ec)) {
      ec.clear();
      continue;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
      if (ec) {
        return false;
      }
      if (!entry.is_regular_file(ec)) {
        continue;
      }

      const auto name = rex::path_to_utf8(entry.path().filename());
      if (AsciiEndsWithCase(name, needle) ||
          rex::string::utf8_lower_ascii(name).find(needle) != std::string::npos) {
        return true;
      }
    }
    ec.clear();
  }
  return false;
}

bool HasPlayerPackageFiles(uint32_t package_id) {
  if (g_game_data_root.empty()) {
    return false;
  }

  const auto data_root = g_game_data_root / "data";
  switch (package_id) {
    case 11:
      return PathExists(data_root / "player" / "dllJulius.dll") ||
             PathExists(data_root / "player" / "player_julius.def");
    case 12:
      return PathExists(data_root / "player" / "dllMaria.dll") ||
             PathExists(data_root / "player" / "player_maria.def");
    case 13:
      return PathExists(data_root / "player" / "dllRichter.dll") ||
             PathExists(data_root / "player" / "player_richter.def") ||
             PathExists(data_root / "opd" / "player" / "ric" / "P_0S_RIC.OPD") ||
             PathExists(data_root / "opd" / "player" / "ric" / "c_0s_ric.col");
    case 14:
      return PathExists(data_root / "player" / "dllYoko.dll") ||
             PathExists(data_root / "player" / "player_yoko.def");
    case 15:
      return PathExists(data_root / "player" / "dllSimon.dll") ||
             PathExists(data_root / "player" / "player_simon.def");
    case 16:
      return PathExists(data_root / "player" / "dllFuma.dll") ||
             PathExists(data_root / "player" / "player_fuma.def");
    default:
      return false;
  }
}

struct LoosePackageDetails {
  bool player = false;
  bool stage = false;
  bool enemy = false;
  bool item = false;
  bool bgm = false;
  bool text = false;
  bool credit = false;
};

LoosePackageDetails ProbeLoosePackage(uint32_t package_id) {
  LoosePackageDetails details;
  if (g_game_data_root.empty()) {
    return details;
  }

  const auto data_root = g_game_data_root / "data";
  details.player = HasPlayerPackageFiles(package_id);
  details.stage =
      PathExists(data_root / "stage" / PackageNumberedFileName("stage", package_id, "_def.dat"));
  details.enemy =
      PathExists(data_root / "enemy" / PackageNumberedFileName("EnemyParam", package_id, ".dat"));
  details.item = AnyDlcItemTable(package_id);
  details.bgm =
      PathExists(data_root / "sound" / PackageNumberedFileName("bgm", package_id, "_def.dat"));
  details.text = AnyFileWithPrefixSuffix(data_root / "text",
                                         PackageNumberedFileName("textres", package_id, "_"),
                                         ".txrc");
  details.credit =
      PathExists(data_root / "credit" / PackageNumberedFileName("credit", package_id, "_def.dat"));
  return details;
}

uint32_t LoosePackageFlags(uint32_t package_id) {
  uint32_t flags = 0;
  const auto details = ProbeLoosePackage(package_id);
  if (details.player) {
    flags |= kPackageFlagPlayer;
  }
  if (details.stage) {
    flags |= kPackageFlagStage;
  }
  if (details.enemy) {
    flags |= kPackageFlagEnemy;
  }
  if (details.item) {
    flags |= kPackageFlagItem;
  }
  if (details.bgm) {
    flags |= kPackageFlagBgm;
  }
  if (details.text) {
    flags |= kPackageFlagText;
  }
  if (details.credit) {
    flags |= kPackageFlagCredit;
  }

  return flags;
}

void AddPackageId(std::vector<uint32_t>& ids, uint32_t id) {
  if (id == 0 || id > 31) {
    return;
  }
  if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
    ids.push_back(id);
  }
}

std::optional<uint32_t> ParsePackageId(std::string_view name) {
  if (!AsciiStartsWithCase(name, "package") || !AsciiEndsWithCase(name, ".dat")) {
    return std::nullopt;
  }

  name.remove_prefix(7);
  name.remove_suffix(4);
  uint32_t id = 0;
  auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), id);
  if (ec != std::errc{} || ptr != name.data() + name.size()) {
    return std::nullopt;
  }
  return id;
}

void AddMarkerPackages(std::vector<uint32_t>& ids, const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      return;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    auto id = ParsePackageId(rex::path_to_utf8(entry.path().filename()));
    if (id) {
      AddPackageId(ids, *id);
    }
  }
}

std::optional<uint32_t> ParseTwoDigitsAfterPrefix(std::string_view name,
                                                  std::string_view prefix) {
  if (!AsciiStartsWithCase(name, prefix)) {
    return std::nullopt;
  }
  name.remove_prefix(prefix.size());
  if (name.size() < 2) {
    return std::nullopt;
  }

  uint32_t id = 0;
  auto digits = name.substr(0, 2);
  auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
  if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return id;
}

void AddLooseContentFilePackage(std::vector<uint32_t>& ids, std::string_view file_name,
                                std::string_view relative_path = {}) {
  for (const auto prefix : {"stage", "textres", "bgm", "enemy", "EnemyParam", "credit"}) {
    if (auto id = ParseTwoDigitsAfterPrefix(file_name, prefix)) {
      AddPackageId(ids, *id);
    }
  }

  auto lower_name = rex::string::utf8_lower_ascii(file_name);
  if (AsciiEndsWithCase(lower_name, ".dat")) {
    auto dlc_pos = lower_name.find("_dlc");
    if (dlc_pos != std::string::npos && lower_name.size() >= dlc_pos + 6) {
      if (auto id = ParseTwoDigitsAfterPrefix(std::string_view(lower_name).substr(dlc_pos + 1),
                                              "dlc")) {
        AddPackageId(ids, *id);
      }
    }
  }

  const auto normalized_path = NormalizeLooseRulePath(relative_path);
  if (PathStartsWithCase(normalized_path, "opd/player/julius/")) {
    AddPackageId(ids, 11);
  } else if (PathStartsWithCase(normalized_path, "opd/player/maria/")) {
    AddPackageId(ids, 12);
  } else if (PathStartsWithCase(normalized_path, "opd/player/ric/")) {
    AddPackageId(ids, 13);
  } else if (PathStartsWithCase(normalized_path, "opd/player/yoko/")) {
    AddPackageId(ids, 14);
  } else if (PathStartsWithCase(normalized_path, "opd/player/simon/")) {
    AddPackageId(ids, 15);
  } else if (PathStartsWithCase(normalized_path, "opd/player/fuma/")) {
    AddPackageId(ids, 16);
  }

  if (PathStartsWithCase(normalized_path, "opd/weapon/wp/") &&
      FileStemIsAny(file_name, {"p_wpaa", "f_wpaa", "c_wpaa", "p_wpab", "f_wpab",
                                "c_wpab", "p_wpac", "f_wpac", "c_wpac"})) {
    AddPackageId(ids, 15);
  } else if (PathStartsWithCase(normalized_path, "opd/weapon/wp/") &&
             FileStemIsAny(file_name, {"p_wpad", "f_wpad", "c_wpad"})) {
    AddPackageId(ids, 16);
  }

  if (AsciiEqualCase(file_name, "dllJulius.dll") ||
      AsciiEqualCase(file_name, "player_julius.def")) {
    AddPackageId(ids, 11);
  } else if (AsciiEqualCase(file_name, "dllMaria.dll") ||
             AsciiEqualCase(file_name, "player_maria.def")) {
    AddPackageId(ids, 12);
  } else if (AsciiEqualCase(file_name, "dllRichter.dll") ||
             AsciiEqualCase(file_name, "player_richter.def") ||
             AsciiEqualCase(file_name, "P_0S_RIC.OPD") ||
             AsciiEqualCase(file_name, "c_0s_ric.col")) {
    AddPackageId(ids, 13);
  } else if (AsciiEqualCase(file_name, "dllYoko.dll") ||
             AsciiEqualCase(file_name, "player_yoko.def")) {
    AddPackageId(ids, 14);
  } else if (AsciiEqualCase(file_name, "dllSimon.dll") ||
             AsciiEqualCase(file_name, "player_simon.def")) {
    AddPackageId(ids, 15);
  } else if (AsciiEqualCase(file_name, "dllFuma.dll") ||
             AsciiEqualCase(file_name, "player_fuma.def")) {
    AddPackageId(ids, 16);
  }
}

bool FileNameBelongsToLoosePackage(std::string_view file_name, uint32_t package_id,
                                   std::string_view relative_path = {}) {
  std::vector<uint32_t> ids;
  AddLooseContentFilePackage(ids, file_name, relative_path);
  return std::find(ids.begin(), ids.end(), package_id) != ids.end();
}

void AddLooseContentFilePackages(std::vector<uint32_t>& ids,
                                 const std::filesystem::path& data_root) {
  std::error_code ec;
  if (!std::filesystem::is_directory(data_root, ec)) {
    return;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(data_root, ec)) {
    if (ec) {
      return;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const auto relative = std::filesystem::relative(entry.path(), data_root, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    AddLooseContentFilePackage(ids, rex::path_to_utf8(entry.path().filename()),
                               rex::path_to_utf8(relative));
  }
}

std::vector<uint32_t> DiscoverLoosePackageIds(const std::filesystem::path& game_root) {
  std::vector<uint32_t> ids;
  AddLooseContentFilePackages(ids, game_root / "data");

  AddMarkerPackages(ids, game_root / "000info");
  AddMarkerPackages(ids, game_root / "data" / "000info");

  std::sort(ids.begin(), ids.end());
  return ids;
}

std::string GuestRelativeDataPath(const std::filesystem::path& relative_path) {
  auto path = rex::path_to_utf8(relative_path);
  std::replace(path.begin(), path.end(), '/', '\\');
  return path;
}

std::string GuestPackagePath(std::string_view root_name,
                             const std::filesystem::path& relative_path) {
  std::string path(root_name);
  if (!path.empty() && path.back() != ':') {
    path.push_back(':');
  }
  path.push_back('\\');
  path += GuestRelativeDataPath(relative_path);
  return path;
}

std::vector<std::string> LoosePackageFileEntries(uint32_t package_id,
                                                 std::string_view root_name) {
  std::vector<std::string> entries;
  if (g_game_data_root.empty() || root_name.empty()) {
    return entries;
  }

  const auto data_root = g_game_data_root / "data";
  std::error_code ec;
  if (!std::filesystem::is_directory(data_root, ec)) {
    return entries;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(data_root, ec)) {
    if (ec) {
      REXLOG_WARN("HoD loose content: stopped package {:02} file scan: {}",
                  package_id, ec.message());
      break;
    }
    if (!entry.is_regular_file(ec)) {
      ec.clear();
      continue;
    }

    const auto file_name = rex::path_to_utf8(entry.path().filename());
    const auto relative_to_data = std::filesystem::relative(entry.path(), data_root, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    if (!FileNameBelongsToLoosePackage(file_name, package_id,
                                       GuestRelativeDataPath(relative_to_data))) {
      continue;
    }

    const auto relative = std::filesystem::relative(entry.path(), g_game_data_root, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    entries.push_back(GuestPackagePath(root_name, relative));
  }

  std::sort(entries.begin(), entries.end(), [](const std::string& lhs, const std::string& rhs) {
    return rex::string::utf8_lower_ascii(lhs) < rex::string::utf8_lower_ascii(rhs);
  });
  entries.erase(std::unique(entries.begin(), entries.end(), [](const std::string& lhs,
                                                               const std::string& rhs) {
                  return AsciiEqualCase(lhs, rhs);
                }),
                entries.end());
  return entries;
}

uint32_t PackageFileVectorCount(uint8_t* base, uint32_t package_record) {
  const auto begin = LoadGuestU32(base, package_record + kPackageFileVectorBeginOffset);
  const auto end = LoadGuestU32(base, package_record + kPackageFileVectorEndOffset);
  if (begin == 0 || end < begin) {
    return 0;
  }
  return (end - begin) / kGameStringSize;
}

bool ReplaceLoosePackageFileVector(PPCContext& ctx, uint8_t* base, uint32_t package_record,
                                   uint32_t package_id, std::string_view root_name) {
  const auto entries = LoosePackageFileEntries(package_id, root_name);
  if (entries.empty()) {
    REXLOG_INFO("HoD loose content: package {:02} has no synthesized file-list entries",
                package_id);
    return false;
  }

  const auto bytes = static_cast<uint32_t>(entries.size() * kGameStringSize);
  const auto vector_begin = AllocateGuest(ctx, base, bytes);
  if (vector_begin == 0) {
    REXLOG_WARN("HoD loose content: package {:02} failed to allocate {} file-list bytes",
                package_id, bytes);
    return false;
  }

  ClearGuestMemory(base, vector_begin, bytes);
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto entry_address = vector_begin + static_cast<uint32_t>(i * kGameStringSize);
    if (!WriteGameString(ctx, base, entry_address, entries[i])) {
      REXLOG_WARN("HoD loose content: package {:02} failed to write file-list entry {}",
                  package_id, i);
      return false;
    }
  }

  const auto old_count = PackageFileVectorCount(base, package_record);
  const auto vector_end = vector_begin + bytes;
  StoreGuestU32(base, package_record + kPackageFileVectorBeginOffset, vector_begin);
  StoreGuestU32(base, package_record + kPackageFileVectorEndOffset, vector_end);
  StoreGuestU32(base, package_record + kPackageFileVectorCapacityOffset, vector_end);

  REXLOG_INFO(
      "HoD loose content: package {:02} file-list entries {}->{} root={}",
      package_id, old_count, entries.size(), root_name);
  for (const auto& entry : entries) {
    REXLOG_INFO("HoD loose content: package {:02} advertises {}", package_id, entry);
  }
  return true;
}

void IncrementModuleLoadCount(uint8_t* base, uint32_t hmodule) {
  if (hmodule == 0) {
    return;
  }

  const auto load_count = LoadGuestU16(base, hmodule + kLoaderDataLoadCountOffset);
  if (load_count != 0xFFFFu) {
    StoreGuestU16(base, hmodule + kLoaderDataLoadCountOffset,
                  static_cast<uint16_t>(load_count + 1));
  }
}

std::optional<rex::system::KernelState::RecompiledModuleInfo> FindRecompiledModuleLoose(
    std::string_view guest_path) {
  auto* kernel_state = REX_KERNEL_STATE();
  if (auto info = kernel_state->FindRecompiledModule(guest_path)) {
    return info;
  }

  auto normalized = rex::system::NormalizeGuestPath(guest_path);
  constexpr std::string_view kDevicePrefix = "device/harddisk0/partition1/";
  if (AsciiStartsWithCase(normalized, kDevicePrefix)) {
    normalized.erase(0, kDevicePrefix.size());
    return kernel_state->FindRecompiledModule(normalized);
  }
  return std::nullopt;
}

bool IsSameRecompiledModule(std::string_view requested_path, std::string_view loaded_path) {
  const auto requested_recomp = FindRecompiledModuleLoose(requested_path);
  if (!requested_recomp || requested_recomp->shared_lib_name.empty()) {
    return false;
  }

  const auto loaded_recomp = FindRecompiledModuleLoose(loaded_path);
  return loaded_recomp && requested_recomp->guest_path == loaded_recomp->guest_path;
}

void PatchEnemyResourceListGetter(rex::Runtime* runtime, std::string_view module_path) {
  if (!runtime || module_path.empty()) {
    return;
  }

  auto* dispatcher = runtime->function_dispatcher();
  if (!dispatcher) {
    return;
  }

  const auto normalized = rex::system::NormalizeGuestPath(module_path);
  for (const auto& patch : kEnemyResourceGetterPatches) {
    if (!AsciiEndsWithCase(normalized, patch.file_name)) {
      continue;
    }

    const auto slot_68_vec2_setter_address = patch.getter_address - 0x38u;
    if (!dispatcher->SetFunction(slot_68_vec2_setter_address, ChodEnemySlot68Getter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x44 getter for {} at 0x{:08X}",
                   patch.file_name, slot_68_vec2_setter_address);
      return;
    }

    if (!dispatcher->SetFunction(patch.getter_address, ChodEnemyResourceListGetter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch resource-list getter for {} at 0x{:08X}",
                   patch.file_name, patch.getter_address);
      return;
    }

    if (!dispatcher->SetFunction(patch.slot_496_pair_setter_address,
                                 ChodEnemySlot496Setter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x1F0 setter for {} at 0x{:08X}",
                   patch.file_name, patch.slot_496_pair_setter_address);
      return;
    }

    if (!dispatcher->SetFunction(patch.slot_500_pair_setter_address,
                                 ChodEnemySlot500Setter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x1F4 setter for {} at 0x{:08X}",
                   patch.file_name, patch.slot_500_pair_setter_address);
      return;
    }

    if (!dispatcher->SetFunction(patch.slot_504_getter_out_address,
                                 ChodEnemySlot504Setter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x1F8 setter for {} at 0x{:08X}",
                   patch.file_name, patch.slot_504_getter_out_address);
      return;
    }

    if (!dispatcher->SetFunction(patch.slot_508_getter_ret_address,
                                 ChodEnemySlot508Setter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x1FC setter for {} at 0x{:08X}",
                   patch.file_name, patch.slot_508_getter_ret_address);
      return;
    }

    const auto slot_528_pair_setter_address = patch.slot_500_pair_setter_address + 0x90u;
    const auto slot_532_pair_setter_address = patch.slot_500_pair_setter_address + 0xA8u;
    const auto slot_536_getter_out_address = patch.slot_500_pair_setter_address + 0xC0u;
    const auto slot_540_getter_out_address = patch.slot_500_pair_setter_address + 0xD8u;
    const auto is_psbeel = std::string_view(patch.file_name) == "dll_psbeel.dll";
    const auto slot_784_name_setter_address =
        is_psbeel ? 0x8BF10B30u : patch.slot_500_pair_setter_address + 0x650u;
    const auto slot_808_entry_pointer_getter_address =
        is_psbeel ? 0x8BF10BD8u : patch.slot_500_pair_setter_address + 0x668u;
    const auto slot_832_indexed_setter_address = patch.slot_500_pair_setter_address + 0x680u;

    if (!dispatcher->SetFunction(slot_528_pair_setter_address,
                                 ChodEnemySlot528Setter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x210 setter for {} at 0x{:08X}",
                   patch.file_name, slot_528_pair_setter_address);
      return;
    }

    if (!dispatcher->SetFunction(slot_532_pair_setter_address,
                                 ChodEnemySlot532Setter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x214 setter for {} at 0x{:08X}",
                   patch.file_name, slot_532_pair_setter_address);
      return;
    }

    if (!dispatcher->SetFunction(slot_536_getter_out_address,
                                 ChodEnemySlot536Setter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x218 setter for {} at 0x{:08X}",
                   patch.file_name, slot_536_getter_out_address);
      return;
    }

    if (!dispatcher->SetFunction(slot_540_getter_out_address,
                                 ChodEnemySlot540Setter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x21C setter for {} at 0x{:08X}",
                   patch.file_name, slot_540_getter_out_address);
      return;
    }

    if (!dispatcher->SetFunction(slot_784_name_setter_address,
                                 is_psbeel ? ChodEnemyPsBeelSlot784NameGetter
                                           : ChodEnemySlot784NameGetter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x310 name getter for {} at 0x{:08X}",
                   patch.file_name, slot_784_name_setter_address);
      return;
    }

    if (!dispatcher->SetFunction(slot_808_entry_pointer_getter_address,
                                 is_psbeel ? ChodEnemyPsBeelSlot808ActiveGetter
                                           : ChodEnemySlot808ActiveGetter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x328 active getter for {} at 0x{:08X}",
                   patch.file_name, slot_808_entry_pointer_getter_address);
      return;
    }

    if (!dispatcher->SetFunction(slot_832_indexed_setter_address,
                                 ChodEnemySlot832CountGetter)) {
      REXLOG_ERROR("HoD enemy DLL: failed to patch slot +0x340 count getter for {} at 0x{:08X}",
                   patch.file_name, slot_832_indexed_setter_address);
      return;
    }

    const bool needs_guarded_legacy_lists = IsGuardedLegacyEnemyModule(patch.file_name);
    const bool is_ps_module = IsPsEnemyModule(patch.file_name);
    if (needs_guarded_legacy_lists) {
      const auto legacy_vector_getter_base = patch.getter_address + 0x320u;
      if (!dispatcher->SetFunction(legacy_vector_getter_base,
                                   ChodEnemyPsLegacyVector252Getter) ||
          !dispatcher->SetFunction(legacy_vector_getter_base + 0x08u,
                                   ChodEnemyPsLegacyVector236Getter) ||
          !dispatcher->SetFunction(legacy_vector_getter_base + 0x10u,
                                   ChodEnemyPsLegacyVector220Getter) ||
          !dispatcher->SetFunction(legacy_vector_getter_base + 0x18u,
                                   ChodEnemyPsLegacyVector268Getter)) {
        REXLOG_ERROR("HoD enemy DLL: failed to patch guarded legacy vectors for {}",
                     patch.file_name);
        return;
      }

      const auto first_list_getter_base = patch.getter_address + 0xD00u;
      if (!dispatcher->SetFunction(first_list_getter_base,
                                   ChodEnemyFirstListU32Offset48Getter) ||
          !dispatcher->SetFunction(first_list_getter_base + 0x18u,
                                   ChodEnemyFirstListU8Offset41Getter) ||
          !dispatcher->SetFunction(first_list_getter_base + 0x30u,
                                   ChodEnemyFirstListU8Offset40Getter) ||
          !dispatcher->SetFunction(first_list_getter_base + 0x48u,
                                   ChodEnemyFirstListU8Offset31Getter) ||
          !dispatcher->SetFunction(first_list_getter_base + 0x60u,
                                   ChodEnemyFirstListU8Offset29Getter) ||
          !dispatcher->SetFunction(first_list_getter_base + 0x78u,
                                   ChodEnemyFirstListU8Offset30Getter) ||
          !dispatcher->SetFunction(first_list_getter_base + 0x90u,
                                   ChodEnemyFirstListU32Offset32Getter) ||
          !dispatcher->SetFunction(first_list_getter_base + 0xA8u,
                                   ChodEnemyFirstListU32Offset36Getter) ||
          !dispatcher->SetFunction(first_list_getter_base + 0xC0u,
                                   ChodEnemyFirstListU32Offset12Getter)) {
        REXLOG_ERROR("HoD enemy DLL: failed to patch guarded first-list getters for {}",
                     patch.file_name);
        return;
      }
      REXLOG_INFO("HoD enemy DLL: patched guarded legacy vectors for {} base=0x{:08X}",
                  patch.file_name, legacy_vector_getter_base);
      REXLOG_INFO("HoD enemy DLL: patched guarded first-list getters for {} base=0x{:08X}",
                  patch.file_name, first_list_getter_base);

      const auto old_stage_vector_base = patch.slot_496_pair_setter_address - 0xF0u;
      if (!dispatcher->SetFunction(old_stage_vector_base,
                                   ChodEnemyPsOldStageVector40Getter) ||
          !dispatcher->SetFunction(old_stage_vector_base + 0x18u,
                                   ChodEnemyPsOldStageVector56Getter) ||
          !dispatcher->SetFunction(old_stage_vector_base + 0x30u,
                                   ChodEnemyPsOldStageVector56SecondGetter) ||
          !dispatcher->SetFunction(old_stage_vector_base + 0x48u,
                                   ChodEnemyPsOldStageVector52Getter) ||
          !dispatcher->SetFunction(old_stage_vector_base + 0x60u,
                                   ChodEnemyPsOldStageVector100Getter)) {
        REXLOG_ERROR("HoD enemy DLL: failed to patch guarded old-stage "
                     "vectors for {}",
                     patch.file_name);
        return;
      }
      REXLOG_INFO("HoD enemy DLL: patched guarded old-stage vectors for {} "
                  "base=0x{:08X}",
                  patch.file_name, old_stage_vector_base);

      struct PsOldStageVectorCompatPatch {
        std::string_view file_name;
        uint32_t slot_412_address;
        uint32_t slot_416_address;
        uint32_t slot_420_address;
        uint32_t slot_424_address;
        uint32_t slot_428_address;
      };
      constexpr PsOldStageVectorCompatPatch kPsOldStageVectorCompatPatches[] = {
          {"dll_psanfauglir.dll", 0x88E101D0u, 0x88E13CC0u, 0x88E13D38u,
           0x88E13DB0u, 0x88E13E28u},
          {"dll_psbeel.dll", 0x8BF15340u, 0x8BF169B0u, 0x8BF16A28u,
           0x8BF16AA0u, 0x8BF16B18u},
          {"dll_psberrigan.dll", 0x88D12E68u, 0x88D14480u, 0x88D144F8u,
           0x88D14570u, 0x88D145E8u},
          {"dll_psfishh.dll", 0x8869C898u, 0x88693E30u, 0x88693EA8u,
           0x88693F20u, 0x88693F98u},
          {"dll_psgaibon.dll", 0x88D901D0u, 0x88D93E20u, 0x88D93E98u,
           0x88D93F10u, 0x88D93F88u},
          {"dll_pskillerf.dll", 0x887104D0u, 0x88713668u, 0x887136E0u,
           0x88713758u, 0x887137D0u},
          {"dll_psukobach.dll", 0x88790480u, 0x88793A18u, 0x88793A90u,
           0x88793B08u, 0x88793B80u},
      };
      if (is_ps_module) {
        for (const auto& compat : kPsOldStageVectorCompatPatches) {
          if (std::string_view(patch.file_name) != compat.file_name) {
            continue;
          }
          if (!RegisterPsOldStageVectorCompatSlot(dispatcher, compat.file_name,
                                                  compat.slot_412_address,
                                                  ChodPsModernOldSlot412Vector40) ||
              !RegisterPsOldStageVectorCompatSlot(dispatcher, compat.file_name,
                                                  compat.slot_416_address,
                                                  ChodPsModernOldSlot416Vector56) ||
              !RegisterPsOldStageVectorCompatSlot(dispatcher, compat.file_name,
                                                  compat.slot_420_address,
                                                  ChodPsModernOldSlot420Vector56) ||
              !RegisterPsOldStageVectorCompatSlot(dispatcher, compat.file_name,
                                                  compat.slot_424_address,
                                                  ChodPsModernOldSlot424Vector52) ||
              !RegisterPsOldStageVectorCompatSlot(dispatcher, compat.file_name,
                                                  compat.slot_428_address,
                                                  ChodPsModernOldSlot428Vector100)) {
            REXLOG_ERROR("HoD enemy DLL: failed to patch PS old-stage vector "
                         "compatibility for {}",
                         compat.file_name);
            return;
          }
          REXLOG_INFO("HoD enemy DLL: patched PS old-stage vector compatibility "
                      "for {}",
                      compat.file_name);
          break;
        }
      }

      const auto second_list_setter_base = patch.getter_address + 0xB68u;
      if (!dispatcher->SetFunction(second_list_setter_base + 0xE0u,
                                   ChodEnemySecondListU32Offset124Setter) ||
          !dispatcher->SetFunction(second_list_setter_base + 0xF8u,
                                   ChodEnemySecondListU32Offset128Setter)) {
        REXLOG_ERROR(
            "HoD enemy DLL: failed to patch guarded second-list setters for {}",
            patch.file_name);
        return;
      }
      REXLOG_INFO("HoD enemy DLL: patched guarded second-list setters for {} "
                  "base=0x{:08X}",
                  patch.file_name, second_list_setter_base);
    }

    REXLOG_INFO("HoD enemy DLL: patched compatibility for {} slot68=0x{:08X} "
                "resource=0x{:08X} "
                "slot496=0x{:08X} slot500=0x{:08X} slot504=0x{:08X} "
                "slot508=0x{:08X} slot528=0x{:08X} slot532=0x{:08X} "
                "slot536=0x{:08X} slot540=0x{:08X} slot784=0x{:08X} "
                "slot808=0x{:08X} slot832=0x{:08X}",
                patch.file_name, slot_68_vec2_setter_address, patch.getter_address,
                patch.slot_496_pair_setter_address, patch.slot_500_pair_setter_address,
                patch.slot_504_getter_out_address, patch.slot_508_getter_ret_address,
                slot_528_pair_setter_address, slot_532_pair_setter_address,
                slot_536_getter_out_address, slot_540_getter_out_address,
                slot_784_name_setter_address,
                slot_808_entry_pointer_getter_address, slot_832_indexed_setter_address);
    return;
  }
}

void LogLoadedModule(std::string_view module_path, uint8_t* base, uint32_t hmodule,
                     uint32_t status, bool recompiled) {
  if (module_path.empty()) {
    return;
  }
  if (!AsciiEndsWithCase(module_path, ".dll")) {
    return;
  }

  if (hmodule == 0) {
    REXLOG_INFO("HoD loose content: XexLoadImage {} -> status=0x{:08X} hmodule=0x00000000 "
                "recompiled={}",
                module_path, status, recompiled);
    return;
  }

  const auto image_base = LoadGuestU32(base, hmodule + kLoaderDataImageBaseOffset);
  const auto image_size = LoadGuestU32(base, hmodule + kLoaderDataImageSizeOffset);
  const auto full_image_size = LoadGuestU32(base, hmodule + kLoaderDataFullImageSizeOffset);
  const auto entry_point = LoadGuestU32(base, hmodule + kLoaderDataEntryPointOffset);
  const auto load_count = LoadGuestU16(base, hmodule + kLoaderDataLoadCountOffset);
  REXLOG_INFO(
      "HoD loose content: XexLoadImage {} -> status=0x{:08X} hmodule=0x{:08X} "
      "image=0x{:08X}+0x{:X}/0x{:X} entry=0x{:08X} load_count={} recompiled={}",
      module_path, status, hmodule, image_base, image_size, full_image_size, entry_point,
      load_count, recompiled);

}

std::string GuestPathFileName(std::string_view path) {
  return rex::string::utf8_find_name_from_guest_path(
      rex::string::utf8_fix_guest_path_separators(path));
}

std::string DeviceQualifiedGamePath(std::string_view path) {
  if (path.empty()) {
    return {};
  }
  if (path[0] == '\\' || path.find(':') != std::string_view::npos) {
    return std::string(path);
  }

  std::string result = "D:\\";
  result.reserve(result.size() + path.size());
  for (const char c : path) {
    result.push_back(c == '/' ? '\\' : c);
  }
  return result;
}

uint32_t WriteTemporaryGuestCString(std::string_view text) {
  auto* memory = REX_KERNEL_MEMORY();
  const auto guest_address = memory->SystemHeapAlloc(static_cast<uint32_t>(text.size() + 1), 1);
  if (guest_address == 0) {
    return 0;
  }

  auto* guest_text = memory->TranslateVirtual<uint8_t*>(guest_address);
  std::memcpy(guest_text, text.data(), text.size());
  guest_text[text.size()] = 0;
  return guest_address;
}

bool CallXexLoadImageWithPath(PPCContext& ctx, uint8_t* base, std::string_view path) {
  const auto guest_path = WriteTemporaryGuestCString(path);
  if (guest_path == 0) {
    REXLOG_WARN("HoD loose content: failed to allocate temporary XexLoadImage path {}", path);
    return false;
  }

  ctx.r3.u64 = guest_path;
  __imp__XexLoadImage(ctx, base);
  REX_KERNEL_MEMORY()->SystemHeapFree(guest_path);
  return true;
}

bool IsLooseFileName(std::string_view file_name, uint32_t* out_id = nullptr) {
  constexpr std::string_view kPrefix = "HoDLoosePackage";
  if (!AsciiStartsWithCase(file_name, kPrefix)) {
    return false;
  }

  file_name.remove_prefix(kPrefix.size());
  uint32_t id = 0;
  auto [ptr, ec] = std::from_chars(file_name.data(), file_name.data() + file_name.size(), id);
  if (ec != std::errc{} || ptr != file_name.data() + file_name.size()) {
    return false;
  }
  if (out_id) {
    *out_id = id;
  }
  return true;
}

std::string LooseFileName(uint32_t package_id) {
  char name[32]{};
  std::snprintf(name, sizeof(name), "HoDLoosePackage%02u", package_id);
  return name;
}

std::u16string LooseDisplayName(uint32_t package_id) {
  char name[32]{};
  std::snprintf(name, sizeof(name), "HoD Loose Content %02u", package_id);
  std::u16string result;
  for (char c : std::string_view(name)) {
    result.push_back(static_cast<char16_t>(c));
  }
  return result;
}

XCONTENT_DATA LooseContentData(uint32_t package_id) {
  XCONTENT_DATA data{};
  data.device_id = static_cast<uint32_t>(DummyDeviceId::HDD);
  data.content_type = rex::system::XContentType(kMarketplaceContent);
  data.set_display_name(LooseDisplayName(package_id));
  data.set_file_name(LooseFileName(package_id));
  return data;
}

std::string RootName(std::string_view root_name) {
  auto root = std::string(root_name);
  if (!root.empty() && root.back() == ':') {
    root.pop_back();
  }
  return root;
}

std::optional<uint32_t> DlcIdFromRoot(std::string_view root_name) {
  auto root = RootName(root_name);
  if (root.size() != 5 || !AsciiStartsWithCase(root, "DLC")) {
    return std::nullopt;
  }

  uint32_t id = 0;
  auto digits = std::string_view(root).substr(3);
  auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
  if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return id;
}

bool IsKnownLoosePackage(uint32_t package_id) {
  const std::lock_guard lock(g_loose_content_mutex);
  return std::find(g_loose_package_ids.begin(), g_loose_package_ids.end(), package_id) !=
         g_loose_package_ids.end();
}

std::vector<uint32_t> LoosePackageIdsSnapshot() {
  const std::lock_guard lock(g_loose_content_mutex);
  return g_loose_package_ids;
}

void RegisterLooseRoot(std::string_view root_name, uint32_t package_id) {
  auto* fs = REX_KERNEL_FS();
  auto root = RootName(root_name) + ":";
  fs->UnregisterSymbolicLink(root);
  fs->RegisterSymbolicLink(root, kGameDataMount);

  const std::lock_guard lock(g_loose_content_mutex);
  auto it = std::find_if(g_mounted_loose_roots.begin(), g_mounted_loose_roots.end(),
                         [&](const LooseMount& mount) {
                           return AsciiEqualCase(mount.root, root);
                         });
  if (it != g_mounted_loose_roots.end()) {
    it->package_id = package_id;
  } else {
    g_mounted_loose_roots.push_back({root, package_id});
  }
}

void UnregisterLooseRoot(std::string_view root_name) {
  auto root = RootName(root_name) + ":";
  REX_KERNEL_FS()->UnregisterSymbolicLink(root);
  const std::lock_guard lock(g_loose_content_mutex);
  std::erase_if(g_mounted_loose_roots, [&](const LooseMount& mount) {
    return AsciiEqualCase(mount.root, root);
  });
}

std::optional<uint32_t> MountedLoosePackageId(std::string_view root_name) {
  auto root = RootName(root_name) + ":";
  const std::lock_guard lock(g_loose_content_mutex);
  auto it = std::find_if(g_mounted_loose_roots.begin(), g_mounted_loose_roots.end(),
                         [&](const LooseMount& mount) {
                           return AsciiEqualCase(mount.root, root);
                         });
  if (it == g_mounted_loose_roots.end()) {
    return std::nullopt;
  }
  return it->package_id;
}

std::string MountedLooseRootForPackageId(uint32_t package_id) {
  const std::lock_guard lock(g_loose_content_mutex);
  auto it = std::find_if(g_mounted_loose_roots.rbegin(), g_mounted_loose_roots.rend(),
                         [&](const LooseMount& mount) {
                           return mount.package_id == package_id;
                         });
  if (it == g_mounted_loose_roots.rend()) {
    return {};
  }
  return it->root;
}

void RegisterLooseDataRootAliases() {
  auto* fs = REX_KERNEL_FS();
  for (uint32_t id = 0; id < 32; ++id) {
    char root[8]{};
    std::snprintf(root, sizeof(root), "DLC%02u:", id);
    fs->UnregisterSymbolicLink(root);
    fs->RegisterSymbolicLink(root, kGameDataMount);
  }
  REXLOG_INFO("HoD loose content: mapped DLC00:-DLC31: to loose data root");
}

void SetActiveLoosePackageId(uint32_t package_id) {
  const std::lock_guard lock(g_loose_content_mutex);
  g_active_loose_package_id = package_id;
}

bool ClearActiveLoosePackageId(uint32_t package_id) {
  const std::lock_guard lock(g_loose_content_mutex);
  if (g_active_loose_package_id == package_id) {
    g_active_loose_package_id.reset();
    return true;
  }
  return false;
}

std::optional<uint32_t> ActiveLoosePackageId() {
  const std::lock_guard lock(g_loose_content_mutex);
  return g_active_loose_package_id;
}

void AppendContentItem(XStaticEnumerator<XCONTENT_DATA>* enumerator, const XCONTENT_DATA& data) {
  auto* item = enumerator->AppendItem();
  *item = data;
}

bool EnumeratorAlreadyHasFileName(const std::vector<std::string>& names, std::string_view name) {
  return std::any_of(names.begin(), names.end(), [&](const std::string& existing) {
    return rex::string::utf8_equal_case(existing, name);
  });
}

u32 ChodXamContentCreateEnumerator_entry(u32 user_index, u32 device_id, u32 content_type,
                                         u32 content_flags, u32 items_per_enumerate,
                                         mapped_u32 buffer_size_ptr, mapped_u32 handle_out) {
  assert_not_null(handle_out);

  auto device_info = device_id == 0 ? nullptr : GetDummyDeviceInfo(device_id);
  if ((device_id && device_info == nullptr) || !handle_out) {
    if (buffer_size_ptr) {
      *buffer_size_ptr = 0;
    }
    return X_E_INVALIDARG;
  }

  if (buffer_size_ptr) {
    *buffer_size_ptr = sizeof(XCONTENT_DATA) * items_per_enumerate;
  }

  auto e = make_object<XStaticEnumerator<XCONTENT_DATA>>(REX_KERNEL_STATE(),
                                                         items_per_enumerate);
  auto result = e->Initialize(0xFF, 0xFE, 0x20005, 0x20007, 0);
  if (XFAILED(result)) {
    return result;
  }

  std::vector<std::string> enumerated_names;
  uint32_t real_count = 0;
  const bool enumerate_hdd = !device_info || device_info->device_id == DummyDeviceId::HDD;
  if (enumerate_hdd) {
    const uint64_t xuid = REX_KERNEL_STATE()->user_profile()->xuid();
    auto* content_manager = REX_KERNEL_STATE()->content_manager();
    auto append_real_content = [&](uint64_t owner_xuid) {
      auto content_datas = content_manager->ListContent(
          static_cast<uint32_t>(DummyDeviceId::HDD), owner_xuid,
          rex::system::XContentType(uint32_t(content_type)));
      for (const auto& content_data : content_datas) {
        const auto file_name = content_data.file_name();
        if (EnumeratorAlreadyHasFileName(enumerated_names, file_name)) {
          continue;
        }
        enumerated_names.push_back(file_name);
        AppendContentItem(e.get(), content_data);
        ++real_count;
      }
    };

    append_real_content(xuid);
    if (xuid != 0) {
      append_real_content(0);
    }
  }

  uint32_t loose_count = 0;
  if (enumerate_hdd && content_type == kMarketplaceContent) {
    for (const auto package_id : LoosePackageIdsSnapshot()) {
      const auto file_name = LooseFileName(package_id);
      if (EnumeratorAlreadyHasFileName(enumerated_names, file_name)) {
        continue;
      }
      AppendContentItem(e.get(), LooseContentData(package_id));
      enumerated_names.push_back(file_name);
      ++loose_count;
    }
  }

  REXLOG_INFO("HoD loose content: entitlement enumerator real={} loose={} total={}",
              real_count, loose_count, e->item_count());

  *handle_out = e->handle();
  return X_ERROR_SUCCESS;
}

u32 ChodXamContentCreateEx_entry(u32 user_index, mapped_string root_name,
                                 mapped_void content_data_ptr, u32 flags,
                                 mapped_u32 disposition_ptr, mapped_u32 license_mask_ptr,
                                 u32 cache_size, u64 content_size, mapped_void overlapped_ptr) {
  auto* content_data = content_data_ptr.as<XCONTENT_DATA*>();
  uint32_t package_id = 0;
  const bool is_loose_content =
      content_data && content_data->content_type == rex::system::XContentType(kMarketplaceContent) &&
      IsLooseFileName(content_data->file_name(), &package_id) && IsKnownLoosePackage(package_id);

  if (content_data && content_data->content_type == rex::system::XContentType(kMarketplaceContent)) {
    REXLOG_INFO("HoD loose content: create root={} file={} flags=0x{:X} loose={}",
                root_name.value(), content_data->file_name(), flags, is_loose_content);
  }

  if (!is_loose_content) {
    return rex::kernel::xam::XamContentCreateEx_entry(user_index, root_name, content_data_ptr,
                                                     flags, disposition_ptr, license_mask_ptr,
                                                     cache_size, content_size, overlapped_ptr);
  }

  RegisterLooseRoot(root_name.value(), package_id);
  SetActiveLoosePackageId(package_id);
  if (disposition_ptr) {
    *disposition_ptr = 2;
  }
  if (license_mask_ptr) {
    *license_mask_ptr = kLooseContentLicenseMask;
  }

  REXLOG_INFO("HoD loose content: opened loose entitlement {:02} as root={}",
              package_id, root_name.value());

  if (overlapped_ptr) {
    REX_KERNEL_STATE()->CompleteOverlappedImmediateEx(overlapped_ptr.guest_address(),
                                                     X_ERROR_SUCCESS, X_ERROR_SUCCESS, 2);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}

u32 ChodXamContentClose_entry(mapped_string root_name, mapped_void overlapped_ptr) {
  if (auto package_id = MountedLoosePackageId(root_name.value())) {
    ClearActiveLoosePackageId(*package_id);
    REXLOG_INFO("HoD loose content: closed loose entitlement root={} package={:02}",
                root_name.value(), *package_id);
    if (overlapped_ptr) {
      REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped_ptr.guest_address(),
                                                     X_ERROR_SUCCESS);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_SUCCESS;
  }
  return rex::kernel::xam::XamContentClose_entry(root_name, overlapped_ptr);
}

}  // namespace

REX_HOOK(sub_82639D80, ChodXamContentCreateEnumerator_entry)
REX_HOOK(sub_82639CD8, ChodXamContentCreateEx_entry)
REX_HOOK(sub_82639D68, ChodXamContentClose_entry)

REX_HOOK_RAW(ChodXexLoadImage) {
  const auto module_path = GuestCString(base, ctx.r3.u32, 512);
  const auto hmodule_out = ctx.r6.u32;
  const auto recompiled_module =
      module_path.empty() ? std::nullopt : FindRecompiledModuleLoose(module_path);
  const bool has_recompiled_module = recompiled_module.has_value();
  if (!module_path.empty()) {
    const auto module_file = GuestPathFileName(module_path);
    if (!module_file.empty()) {
      auto existing_module = REX_KERNEL_STATE()->GetModule(module_file, true);
      if (existing_module && IsSameRecompiledModule(module_path, existing_module->path())) {
        const auto hmodule = existing_module->hmodule_ptr();
        if (ctx.r6.u32 != 0) {
          StoreGuestU32(base, ctx.r6.u32, hmodule);
        }
        IncrementModuleLoadCount(base, hmodule);
        REXLOG_INFO("HoD loose content: aliased duplicate XexLoadImage {} to loaded {}",
                    module_path, existing_module->path());
        LogLoadedModule(module_path, base, hmodule, 0, true);
        PatchEnemyResourceListGetter(rex::Runtime::instance(), module_path);
        ctx.r3.u64 = 0;
        return;
      }
    }
  }

  if (recompiled_module && !recompiled_module->guest_path.empty()) {
    const auto normalized_request = rex::system::NormalizeGuestPath(module_path);
    if (normalized_request != recompiled_module->guest_path) {
      const auto remapped_path = DeviceQualifiedGamePath(recompiled_module->guest_path);
      REXLOG_INFO("HoD loose content: XexLoadImage remapped {} -> {}", module_path,
                  remapped_path);
      if (!CallXexLoadImageWithPath(ctx, base, remapped_path)) {
        __imp__XexLoadImage(ctx, base);
      }
    } else {
      __imp__XexLoadImage(ctx, base);
    }
  } else {
    __imp__XexLoadImage(ctx, base);
  }

  const auto status = ctx.r3.u32;
  const auto hmodule = hmodule_out == 0 ? 0 : LoadGuestU32(base, hmodule_out);
  LogLoadedModule(module_path, base, hmodule, status, has_recompiled_module);
  if (status == 0) {
    PatchEnemyResourceListGetter(rex::Runtime::instance(), module_path);
  }
}

void ChodLoadImageWrapper(PPCContext& ctx, uint8_t* base, uint32_t min_version) {
  const auto module_path_guest = ctx.r3.u32;
  if (module_path_guest == 0 || ctx.r1.u32 < 16) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto hmodule_out = ctx.r1.u32 - 16;
  StoreGuestU32(base, hmodule_out, 0);

  rex::CallFrame frame(ctx);
  frame.ctx.lr = ctx.lr;
  frame.ctx.r3.u64 = module_path_guest;
  frame.ctx.r4.u64 = 9;
  frame.ctx.r5.u64 = min_version;
  frame.ctx.r6.u64 = hmodule_out;
  ChodXexLoadImage(frame.ctx, base);

  const auto status = frame.ctx.r3.u32;
  if (static_cast<int32_t>(status) < 0) {
    REXLOG_WARN("HoD loose content: load wrapper {} failed status=0x{:08X}",
                GuestCString(base, module_path_guest, 512), status);
    ctx.r3.u64 = 0;
    return;
  }

  ctx.r3.u64 = LoadGuestU32(base, hmodule_out);
}

REX_HOOK_RAW(sub_8263A130) {
  ChodLoadImageWrapper(ctx, base, 0);
}

REX_HOOK_RAW(sub_8263A178) {
  ChodLoadImageWrapper(ctx, base, ctx.r4.u32);
}

uint32_t ResolveChodUserExport(const rex::system::UserModule& module, uint16_t ordinal,
                               uint32_t guest_address, uint32_t caller_address) {
  (void)module;
  (void)ordinal;
  (void)caller_address;
  return guest_address;
}

REX_HOOK_RAW(sub_82454040) {
  REXLOG_INFO(
      "HoD DLC manifest base builder: stem={} check={} root={} package={} limit={}",
      GuestCString(base, ctx.r5.u32), GuestCString(base, ctx.r6.u32),
      GuestCString(base, ctx.r8.u32), ctx.r9.u32, ctx.r7.u32);
  __imp__sub_82454040(ctx, base);
}

REX_HOOK_RAW(sub_824541E8) {
  REXLOG_INFO(
      "HoD DLC manifest builder: stem={} tag={} root={} package={} group={}",
      GuestCString(base, ctx.r4.u32), GuestCString(base, ctx.r5.u32),
      GuestCString(base, ctx.r7.u32), ctx.r8.u32, ctx.r6.u32);
  __imp__sub_824541E8(ctx, base);
}

REX_HOOK_RAW(sub_82580878) {
  const auto object = ctx.r3.u32;
  const auto path = GuestCString(base, ctx.r4.u32, 512);
  const bool trace = HasDlcManifestName(path);
  if (trace) {
    REXLOG_INFO("HoD DLC manifest parse begin: object=0x{:08X} path={}", object, path);
  }

  __imp__sub_82580878(ctx, base);

  if (trace && object) {
    const auto payload = LoadGuestU32(base, object + 16);
    REXLOG_INFO("HoD DLC manifest parse end: object=0x{:08X} payload=0x{:08X}",
                object, payload);
  }
}

REX_HOOK_RAW(sub_82639E18) {
  REXLOG_INFO("HoD loose content: create wrapper root={} data=0x{:08X} flags=0x{:X}",
              reinterpret_cast<const char*>(base + ctx.r4.u32), ctx.r5.u32, ctx.r6.u32);
  __imp__sub_82639E18(ctx, base);
}

REX_HOOK_RAW(sub_8263A878) {
  const auto handle = ctx.r3.u32;
  const auto buffer = ctx.r4.u32;
  const auto buffer_size = ctx.r5.u32;
  __imp__sub_8263A878(ctx, base);
  REXLOG_INFO("HoD loose content: enumerate handle=0x{:08X} buffer=0x{:08X} size={} result=0x{:08X}",
              handle, buffer, buffer_size, ctx.r3.u32);
}

REX_HOOK_RAW(sub_8257E878) {
  const auto package_record = ctx.r4.u32;
  const bool top_level_parse = ctx.r5.u32 == 0;
  const auto loose_package_id = ActiveLoosePackageId();
  __imp__sub_8257E878(ctx, base);
  if (!top_level_parse || !loose_package_id || package_record == 0 ||
      !IsKnownLoosePackage(*loose_package_id)) {
    return;
  }

  const auto root_name = MountedLooseRootForPackageId(*loose_package_id);
  const auto parsed_id = LoadGuestU32(base, package_record + 32);
  StoreGuestU32(base, package_record + 32, *loose_package_id);
  const auto old_flags = LoadGuestU32(base, package_record + 8);
  const auto loose_flags = LoosePackageFlags(*loose_package_id);
  if (loose_flags != 0) {
    const auto new_flags = (old_flags & ~kLoosePackageFamilyMask) | loose_flags;
    StoreGuestU32(base, package_record + 8, new_flags);
    REXLOG_INFO(
        "HoD loose content: loose entitlement classifier root={} parsed_id={:02} package={:02} "
        "flags=0x{:X}->0x{:X}",
        root_name, parsed_id, *loose_package_id, old_flags, new_flags);
  }

  if (!root_name.empty()) {
    ReplaceLoosePackageFileVector(ctx, base, package_record, *loose_package_id, root_name);
  }
}

REX_HOOK_RAW(sub_8257F710) {
  if (auto loose_package_id = ActiveLoosePackageId()) {
    ctx.r3.u64 = *loose_package_id;
    return;
  }
  __imp__sub_8257F710(ctx, base);
}

REX_HOOK_RAW(sub_8238A1E8) {
  static std::atomic_bool logged{false};
  if (!logged.exchange(true)) {
    REXLOG_WARN("HoD loose content: stubbed missing DLC gameplay callback 0x8238A1E8");
  }
  ctx.r3.u64 = 0;
}

REX_HOOK_RAW(ChodResourceObjectAllocTrace) {
  const auto this_ptr = ctx.r3.u32;
  const auto name = GuestCString(base, ctx.r4.u32, 256);
  auto previous_resource_name = std::move(g_traced_resource_object_name);
  const auto previous_resource_arg5 = g_traced_resource_object_arg5;
  g_traced_resource_object_name = name;
  g_traced_resource_object_arg5 = ctx.r5.u32;
  REXLOG_INFO(
      "HoD resource trace: enter sub_82249368 this=0x{:08X} name_ptr=0x{:08X} "
      "name='{}' arg5=0x{:08X} caller=0x{:08X}",
      this_ptr, ctx.r4.u32, name, ctx.r5.u32, static_cast<uint32_t>(ctx.lr));

  const auto heap = LoadGuestU32(base, kGameHeapGlobals + kGameHeapOffset);
  REXLOG_INFO(
      "HoD resource trace: prealloc sub_82249368 heap=0x{:08X} request=0x{:X} "
      "allocator_total=0x{:X} resource='{}'",
      heap, kResourceObjectAllocUserSize, kResourceObjectAllocTotalSize, name);
  LogAllocatorChainSnapshot(base, heap, kResourceObjectAllocTotalSize, name);

  __imp__sub_82249368(ctx, base);

  REXLOG_INFO("HoD resource trace: leave sub_82249368 this=0x{:08X} object=0x{:08X}",
              this_ptr, this_ptr == 0 ? 0 : LoadGuestU32(base, this_ptr + 4));
  g_traced_resource_object_name = std::move(previous_resource_name);
  g_traced_resource_object_arg5 = previous_resource_arg5;
}

REX_HOOK_RAW(ChodHeapTreeAllocTrace) {
  if (g_traced_heap_alloc_depth != 0) {
    REXLOG_INFO(
        "HoD heap trace: allocator enter caller=0x{:08X} ({}) heap=0x{:08X} "
        "wrapper_request=0x{:X} wrapper_align=0x{:X} allocator_total=0x{:X} "
        "lr=0x{:08X} resource='{}'",
        g_traced_heap_alloc_caller, HeapAllocCallerName(g_traced_heap_alloc_caller),
        ctx.r3.u32, g_traced_heap_alloc_request, g_traced_heap_alloc_align,
        ctx.r4.u32, static_cast<uint32_t>(ctx.lr), g_traced_resource_object_name);
    LogHeapAllocatorSnapshot(base, ctx.r3.u32, ctx.r4.u32);
  }

  RepairHeapTreeSingletonBacklink(base, ctx.r3.u32, ctx.r4.u32,
                                  g_traced_resource_object_name);

  __imp__sub_825DD4C8(ctx, base);
}

REX_HOOK_RAW(ChodBackendAllocTrace) {
  const auto backend = ctx.r3.u32;
  const auto wrapper_request = ctx.r4.u32;
  const auto align = ctx.r5.u32;
  const auto internal_heap =
      IsLikelyGuestPointer(backend) ? LoadGuestU32(base, backend + 40) : 0u;
  const auto trace_large_game_heap_alloc =
      internal_heap == 0x40510090u && wrapper_request >= 0x600u;
  const auto should_trace =
      !g_traced_resource_object_name.empty() || g_traced_heap_alloc_depth != 0 ||
      trace_large_game_heap_alloc;
  const auto allocator_request = BackendAllocatorRequest(wrapper_request, align);
  if (should_trace) {
    const auto internal_request = wrapper_request + kBackendAllocHeaderSize;
    const auto normalized_align = NormalizeHeapAlignment(align);
    REXLOG_INFO(
        "HoD heap trace: backend enter caller=0x{:08X} backend=0x{:08X} "
        "wrapper_caller=0x{:08X} ({}) "
        "internal_heap=0x{:08X} wrapper_request=0x{:X} align=0x{:X} "
        "normalized_align=0x{:X} internal_request=0x{:X} "
        "allocator_request=0x{:X} resource='{}' resource_arg5=0x{:X}",
        static_cast<uint32_t>(ctx.lr), backend, g_traced_heap_alloc_caller,
        HeapAllocCallerName(g_traced_heap_alloc_caller), internal_heap, wrapper_request,
        align, normalized_align, internal_request, allocator_request,
        g_traced_resource_object_name,
        g_traced_resource_object_arg5);
    LogHeapAllocatorSnapshot(base, internal_heap, allocator_request);
  }

  RepairHeapTreeSingletonBacklink(base, internal_heap, allocator_request,
                                  g_traced_resource_object_name);

  __imp__sub_825DB208(ctx, base);

  if (should_trace) {
    REXLOG_INFO(
        "HoD heap trace: backend leave backend=0x{:08X} result=0x{:08X} "
        "resource='{}' resource_arg5=0x{:X}",
        backend, ctx.r3.u32, g_traced_resource_object_name,
        g_traced_resource_object_arg5);
    LogHeapAllocatorSnapshot(base, internal_heap, allocator_request);
  }
}

REX_HOOK_RAW(ChodHeapAllocWrapperTrace) {
  const auto caller = static_cast<uint32_t>(ctx.lr);
  const auto should_trace = ShouldTraceHeapAllocCaller(caller);
  const auto previous_caller = g_traced_heap_alloc_caller;
  const auto previous_request = g_traced_heap_alloc_request;
  const auto previous_align = g_traced_heap_alloc_align;
  const auto previous_depth = g_traced_heap_alloc_depth;

  if (should_trace) {
    g_traced_heap_alloc_caller = caller;
    g_traced_heap_alloc_request = ctx.r4.u32;
    g_traced_heap_alloc_align = ctx.r5.u32;
    g_traced_heap_alloc_depth = previous_depth + 1;
    REXLOG_INFO(
        "HoD heap trace: wrapper enter caller=0x{:08X} ({}) heap=0x{:08X} "
        "request=0x{:X} align=0x{:X} resource='{}'",
        caller, HeapAllocCallerName(caller), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
        g_traced_resource_object_name);
  }

  __imp__sub_825E02A0(ctx, base);

  if (should_trace) {
    REXLOG_INFO(
        "HoD heap trace: wrapper leave caller=0x{:08X} result=0x{:08X} resource='{}'",
        caller, ctx.r3.u32, g_traced_resource_object_name);
    g_traced_heap_alloc_caller = previous_caller;
    g_traced_heap_alloc_request = previous_request;
    g_traced_heap_alloc_align = previous_align;
    g_traced_heap_alloc_depth = previous_depth;
  }
}

REX_HOOK_RAW(ChodGameAllocCallbackTrace) {
  const auto caller = static_cast<uint32_t>(ctx.lr);
  const auto request = ctx.r3.u32;
  const auto should_trace = ShouldTraceHeapAllocCaller(caller);
  const auto previous_caller = g_traced_heap_alloc_caller;
  const auto previous_request = g_traced_heap_alloc_request;
  const auto previous_align = g_traced_heap_alloc_align;
  const auto previous_depth = g_traced_heap_alloc_depth;

  if (should_trace) {
    const auto heap = LoadGuestU32(base, 0x828F5664u);
    g_traced_heap_alloc_caller = caller;
    g_traced_heap_alloc_request = request;
    g_traced_heap_alloc_align = 4;
    g_traced_heap_alloc_depth = previous_depth + 1;
    REXLOG_INFO(
        "HoD heap trace: game alloc callback enter caller=0x{:08X} ({}) "
        "request=0x{:X} heap=0x{:08X} resource='{}'",
        caller, HeapAllocCallerName(caller), request, heap,
        g_traced_resource_object_name);
    LogAllocatorChainSnapshot(base, heap, request, g_traced_resource_object_name);
  }

  __imp__sub_8246CBA8(ctx, base);

  if (should_trace) {
    REXLOG_INFO(
        "HoD heap trace: game alloc callback leave caller=0x{:08X} "
        "result=0x{:08X} resource='{}'",
        caller, ctx.r3.u32, g_traced_resource_object_name);
    g_traced_heap_alloc_caller = previous_caller;
    g_traced_heap_alloc_request = previous_request;
    g_traced_heap_alloc_align = previous_align;
    g_traced_heap_alloc_depth = previous_depth;
  }
}

REX_HOOK_RAW(ChodStageEnemyLegacyListTrace) {
  const auto stage = ctx.r3.u32;
  const auto owner_slot =
      IsLikelyGuestPointer(stage) ? stage + kStageLegacyEnemyListOwnerOffset : 0u;
  const auto owner =
      owner_slot != 0 && IsLikelyGuestPointer(owner_slot) ? LoadGuestU32(base, owner_slot) : 0u;
  const auto vtable = IsLikelyGuestPointer(owner) ? LoadGuestU32(base, owner) : 0u;
  const auto slot_356 =
      IsLikelyGuestPointer(vtable) ? LoadGuestU32(base, vtable + 356) : 0u;
  const auto slot_360 =
      IsLikelyGuestPointer(vtable) ? LoadGuestU32(base, vtable + 360) : 0u;
  const auto slot_364 =
      IsLikelyGuestPointer(vtable) ? LoadGuestU32(base, vtable + 364) : 0u;
  const auto slot_368 =
      IsLikelyGuestPointer(vtable) ? LoadGuestU32(base, vtable + 368) : 0u;

  static std::atomic<uint32_t> list_logs{0};
  const auto log_index = list_logs.fetch_add(1);
  if (log_index < 128) {
    REXLOG_INFO(
        "HoD stage trace: sub_82245760 caller=0x{:08X} stage=0x{:08X} "
        "owner_slot=0x{:08X} owner=0x{:08X} vtable=0x{:08X} "
        "slot356=0x{:08X}({}) slot360=0x{:08X}({}) "
        "slot364=0x{:08X}({}) slot368=0x{:08X}({})",
        static_cast<uint32_t>(ctx.lr), stage, owner_slot, owner, vtable, slot_356,
        EnemyPatchNameForAddress(slot_356), slot_360, EnemyPatchNameForAddress(slot_360),
        slot_364, EnemyPatchNameForAddress(slot_364), slot_368,
        EnemyPatchNameForAddress(slot_368));
  }

  __imp__sub_82245760(ctx, base);
}

REX_HOOK_RAW(ChodEnemyResourceListGetter) {
  static std::atomic_bool logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO(
        "HoD enemy DLL: vtable slot +0x50 redirected to object resource-list "
        "vector at caller=0x{:08X} object=0x{:08X}",
        static_cast<uint32_t>(ctx.lr), ctx.r3.u32);
  }

  ctx.r3.u64 = ctx.r3.u32 == 0 ? 0 : ctx.r3.u32 + 456;
}

REX_HOOK_RAW(ChodEnemyFirstListU32Offset12Getter) {
  ctx.r3.u64 = LoadEnemyEntryU32(base, ctx.r3.u32, ctx.r4.u32, 696,
                                 ctx.r4.u32, ctx.r5.u32, 12,
                                 "first-list u32+12",
                                 static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyFirstListU32Offset32Getter) {
  ctx.r3.u64 = LoadEnemyEntryU32(base, ctx.r3.u32, ctx.r4.u32, 696,
                                 ctx.r4.u32, ctx.r5.u32, 32,
                                 "first-list u32+32",
                                 static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyFirstListU32Offset36Getter) {
  ctx.r3.u64 = LoadEnemyEntryU32(base, ctx.r3.u32, ctx.r4.u32, 696,
                                 ctx.r4.u32, ctx.r5.u32, 36,
                                 "first-list u32+36",
                                 static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyFirstListU32Offset48Getter) {
  ctx.r3.u64 = LoadEnemyEntryU32(base, ctx.r3.u32, ctx.r4.u32, 696,
                                 ctx.r4.u32, ctx.r5.u32, 48,
                                 "first-list u32+48",
                                 static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyFirstListU8Offset29Getter) {
  ctx.r3.u64 = LoadEnemyEntryU8(base, ctx.r3.u32, ctx.r4.u32, 696,
                                ctx.r4.u32, ctx.r5.u32, 29,
                                "first-list u8+29",
                                static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyFirstListU8Offset30Getter) {
  ctx.r3.u64 = LoadEnemyEntryU8(base, ctx.r3.u32, ctx.r4.u32, 696,
                                ctx.r4.u32, ctx.r5.u32, 30,
                                "first-list u8+30",
                                static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyFirstListU8Offset31Getter) {
  ctx.r3.u64 = LoadEnemyEntryU8(base, ctx.r3.u32, ctx.r4.u32, 696,
                                ctx.r4.u32, ctx.r5.u32, 31,
                                "first-list u8+31",
                                static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyFirstListU8Offset40Getter) {
  ctx.r3.u64 = LoadEnemyEntryU8(base, ctx.r3.u32, ctx.r4.u32, 696,
                                ctx.r4.u32, ctx.r5.u32, 40,
                                "first-list u8+40",
                                static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyFirstListU8Offset41Getter) {
  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x82241788u) {
    const auto vector = ReadEnemyEntryVector(base, ctx.r3.u32, 696);
    static std::atomic<uint32_t> count_logs{0};
    const auto log_index = count_logs.fetch_add(1);
    if (log_index < 64 || !vector.valid || vector.count > 256u) {
      REXLOG_INFO(
          "HoD enemy DLL: first-list u8+41 legacy count getter "
          "caller=0x{:08X} object=0x{:08X} begin=0x{:08X} end=0x{:08X} "
          "count={} valid={}",
          caller, ctx.r3.u32, vector.begin, vector.end, vector.count, vector.valid);
    }

    ctx.r3.u64 = vector.valid ? vector.count : 0;
    return;
  }

  ctx.r3.u64 = LoadEnemyEntryU8(base, ctx.r3.u32, ctx.r4.u32, 696,
                                ctx.r4.u32, ctx.r5.u32, 41,
                                "first-list u8+41", caller);
}

REX_HOOK_RAW(ChodEnemySecondListU32Offset124Setter) {
  StoreEnemyEntryU32Checked(base, ctx.r3.u32, 712, ctx.r4.u32, 124,
                            ctx.r5.u32, "second-list u32+124 setter",
                            static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemySecondListU32Offset128Setter) {
  StoreEnemyEntryU32Checked(base, ctx.r3.u32, 712, ctx.r4.u32, 128,
                            ctx.r5.u32, "second-list u32+128 setter",
                            static_cast<uint32_t>(ctx.lr));
}

REX_HOOK_RAW(ChodEnemyPsLegacyVector220Getter) {
  ctx.r3.u64 = GuardEnemyLegacyVector(base, static_cast<uint32_t>(ctx.lr),
                                      ctx.r3.u32, 220, 12, 512,
                                      "PS legacy vector +220 / old slot +0x16C");
}

REX_HOOK_RAW(ChodEnemyPsLegacyVector236Getter) {
  ctx.r3.u64 = GuardEnemyLegacyVector(base, static_cast<uint32_t>(ctx.lr),
                                      ctx.r3.u32, 236, 12, 512,
                                      "PS legacy vector +236 / old slot +0x168");
}

REX_HOOK_RAW(ChodEnemyPsLegacyVector252Getter) {
  ctx.r3.u64 = GuardEnemyLegacyVector(base, static_cast<uint32_t>(ctx.lr),
                                      ctx.r3.u32, 252, 12, 512,
                                      "PS legacy vector +252 / old slot +0x164");
}

REX_HOOK_RAW(ChodEnemyPsLegacyVector268Getter) {
  ctx.r3.u64 = GuardEnemyLegacyVector(base, static_cast<uint32_t>(ctx.lr),
                                      ctx.r3.u32, 268, 4, 2048,
                                      "PS legacy vector +268 / old slot +0x170");
}

REX_HOOK_RAW(ChodEnemyPsOldStageVector40Getter) {
  HandlePsOldStageVectorOrSetOnce(ctx, base, 76, 0x19C, 40,
                                  "PS old-stage vector +0x19C");
}

REX_HOOK_RAW(ChodEnemyPsOldStageVector56Getter) {
  HandlePsOldStageVectorOrSetOnce(ctx, base, 80, 0x1A0, 56,
                                  "PS old-stage vector +0x1A0");
}

REX_HOOK_RAW(ChodEnemyPsOldStageVector56SecondGetter) {
  HandlePsOldStageVectorOrSetOnce(ctx, base, 84, 0x1A4, 56,
                                  "PS old-stage vector +0x1A4");
}

REX_HOOK_RAW(ChodEnemyPsOldStageVector52Getter) {
  HandlePsOldStageVectorOrSetOnce(ctx, base, 88, 0x1A8, 52,
                                  "PS old-stage vector +0x1A8");
}

REX_HOOK_RAW(ChodEnemyPsOldStageVector100Getter) {
  HandlePsOldStageVectorOrSetOnce(ctx, base, 92, 0x1AC, 100,
                                  "PS old-stage vector +0x1AC");
}

REX_HOOK_RAW(ChodPsModernOldSlot412Vector40) {
  HandlePsModernOldSlotVector(ctx, base, 0x19C, 40,
                              "PS modern old slot +0x19C");
}

REX_HOOK_RAW(ChodPsModernOldSlot416Vector56) {
  HandlePsModernOldSlotVector(ctx, base, 0x1A0, 56,
                              "PS modern old slot +0x1A0");
}

REX_HOOK_RAW(ChodPsModernOldSlot420Vector56) {
  HandlePsModernOldSlotVector(ctx, base, 0x1A4, 56,
                              "PS modern old slot +0x1A4");
}

REX_HOOK_RAW(ChodPsModernOldSlot424Vector52) {
  HandlePsModernOldSlotVector(ctx, base, 0x1A8, 52,
                              "PS modern old slot +0x1A8");
}

REX_HOOK_RAW(ChodPsModernOldSlot428Vector100) {
  HandlePsModernOldSlotVector(ctx, base, 0x1AC, 100,
                              "PS modern old slot +0x1AC");
}

REX_HOOK_RAW(ChodEnemySlot68Getter) {
  if (ctx.r3.u32 == 0 || ctx.r4.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x822444D8u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x44 redirected to legacy vec2 "
                  "getter at caller=0x{:08X} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X}",
                  caller, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
    }

    const auto x = LoadGuestU32(base, ctx.r4.u32 + 17424);
    const auto y = LoadGuestU32(base, ctx.r4.u32 + 17428);
    StoreGuestU32(base, ctx.r3.u32, x);
    StoreGuestU32(base, ctx.r3.u32 + 4, y);
    if (ctx.r5.u32 >= 0x10000u) {
      StoreGuestU32(base, ctx.r5.u32, x);
      StoreGuestU32(base, ctx.r5.u32 + 4, y);
    }
    return;
  }

  ctx.fpscr.disableFlushMode();
  StoreGuestU32(base, ctx.r3.u32 + 17424, LoadGuestU32(base, ctx.r4.u32));
  StoreGuestU32(base, ctx.r3.u32 + 17428, LoadGuestU32(base, ctx.r4.u32 + 4));
}

REX_HOOK_RAW(ChodEnemySlot496Setter) {
  if (ctx.r3.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x82244114u || ctx.r5.u32 < 0x10000u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x1F0 redirected to legacy u32 "
                  "setter at caller=0x{:08X} r5=0x{:08X}",
                  caller, ctx.r5.u32);
    }

    StoreGuestU32(base, ctx.r3.u32 + 476, ctx.r4.u32);
    return;
  }

  ctx.fpscr.disableFlushMode();
  StoreGuestU32(base, ctx.r3.u32 + 180, LoadGuestU32(base, ctx.r4.u32));
  StoreGuestU32(base, ctx.r3.u32 + 184, LoadGuestU32(base, ctx.r5.u32));
}

REX_HOOK_RAW(ChodEnemySlot500Setter) {
  if (ctx.r3.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x8224412Cu || ctx.r5.u32 < 0x10000u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x1F4 redirected to legacy f32 "
                  "setter at caller=0x{:08X} r5=0x{:08X}",
                  caller, ctx.r5.u32);
    }

    PPCRegister temp{};
    temp.f32 = float(ctx.f1.f64);
    StoreGuestU32(base, ctx.r3.u32 + 480, temp.u32);
    return;
  }

  ctx.fpscr.disableFlushMode();
  StoreGuestU32(base, ctx.r3.u32 + 156, LoadGuestU32(base, ctx.r4.u32));
  StoreGuestU32(base, ctx.r3.u32 + 160, LoadGuestU32(base, ctx.r5.u32));
}

REX_HOOK_RAW(ChodEnemySlot504Setter) {
  if (ctx.r3.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x82244144u || ctx.r4.u32 < 0x10000u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x1F8 redirected to legacy f32 "
                  "setter at caller=0x{:08X} r4=0x{:08X}",
                  caller, ctx.r4.u32);
    }

    PPCRegister temp{};
    temp.f32 = float(ctx.f1.f64);
    StoreGuestU32(base, ctx.r3.u32 + 488, temp.u32);
    return;
  }

  ctx.fpscr.disableFlushMode();
  StoreGuestU32(base, ctx.r4.u32, LoadGuestU32(base, ctx.r3.u32 + 152));
}

REX_HOOK_RAW(ChodEnemySlot508Setter) {
  if (ctx.r3.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x822440FCu) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x1FC redirected to legacy f32 "
                  "setter at caller=0x{:08X}",
                  caller);
    }

    PPCRegister temp{};
    temp.f32 = float(ctx.f1.f64);
    StoreGuestU32(base, ctx.r3.u32 + 492, temp.u32);
    return;
  }

  ctx.fpscr.disableFlushMode();
  PPCRegister temp{};
  temp.u32 = LoadGuestU32(base, ctx.r3.u32 + 152);
  ctx.f1.f64 = double(temp.f32);
}

REX_HOOK_RAW(ChodEnemySlot528Setter) {
  if (ctx.r3.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x82244170u || ctx.r4.u32 < 0x10000u || ctx.r5.u32 < 0x10000u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x210 redirected to legacy u8 "
                  "setter at caller=0x{:08X} r4=0x{:08X} r5=0x{:08X}",
                  caller, ctx.r4.u32, ctx.r5.u32);
    }

    StoreGuestU8(base, ctx.r3.u32 + 0x43, ctx.r4.u8);
    return;
  }

  ctx.fpscr.disableFlushMode();
  StoreGuestU32(base, ctx.r3.u32 + 188, LoadGuestU32(base, ctx.r4.u32));
  StoreGuestU32(base, ctx.r3.u32 + 192, LoadGuestU32(base, ctx.r5.u32));
}

REX_HOOK_RAW(ChodEnemySlot532Setter) {
  if (ctx.r3.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x8224419Cu || ctx.r4.u32 < 0x10000u || ctx.r5.u32 < 0x10000u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x214 redirected to legacy u8 "
                  "setter at caller=0x{:08X} r4=0x{:08X} r5=0x{:08X}",
                  caller, ctx.r4.u32, ctx.r5.u32);
    }

    StoreGuestU8(base, ctx.r3.u32 + 0x44, ctx.r4.u8);
    return;
  }

  ctx.fpscr.disableFlushMode();
  StoreGuestU32(base, ctx.r3.u32 + 196, LoadGuestU32(base, ctx.r4.u32));
  StoreGuestU32(base, ctx.r3.u32 + 200, LoadGuestU32(base, ctx.r5.u32));
}

REX_HOOK_RAW(ChodEnemySlot536Setter) {
  if (ctx.r3.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x822441B4u || ctx.r4.u32 < 0x10000u || ctx.r5.u32 < 0x10000u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x218 redirected to legacy u16 "
                  "setter at caller=0x{:08X} r4=0x{:08X} r5=0x{:08X}",
                  caller, ctx.r4.u32, ctx.r5.u32);
    }

    StoreGuestU16(base, ctx.r3.u32 + 0x222, ctx.r4.u16);
    return;
  }

  ctx.fpscr.disableFlushMode();
  StoreGuestU32(base, ctx.r4.u32, LoadGuestU32(base, ctx.r3.u32 + 188));
  StoreGuestU32(base, ctx.r5.u32, LoadGuestU32(base, ctx.r3.u32 + 192));
}

REX_HOOK_RAW(ChodEnemySlot540Setter) {
  if (ctx.r3.u32 == 0) {
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x822441CCu || ctx.r4.u32 < 0x10000u || ctx.r5.u32 < 0x10000u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x21C redirected to legacy u8 "
                  "setter at caller=0x{:08X} r4=0x{:08X} r5=0x{:08X}",
                  caller, ctx.r4.u32, ctx.r5.u32);
    }

    StoreGuestU8(base, ctx.r3.u32 + 0x229, ctx.r4.u8);
    return;
  }

  ctx.fpscr.disableFlushMode();
  StoreGuestU32(base, ctx.r4.u32, LoadGuestU32(base, ctx.r3.u32 + 196));
  StoreGuestU32(base, ctx.r5.u32, LoadGuestU32(base, ctx.r3.u32 + 200));
}

REX_HOOK_RAW(ChodEnemyPsBeelSlot784NameGetter) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x822411F4u) {
    const auto entries = LoadGuestU32(base, ctx.r3.u32 + 712);
    if (entries == 0) {
      ctx.r3.u64 = 0;
      return;
    }

    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: PsBeel vtable slot +0x310 redirected to "
                  "legacy second-entry name getter at caller=0x{:08X}",
                  caller);
    }

    const auto end = LoadGuestU32(base, ctx.r3.u32 + 716);
    const auto count = end >= entries ? (end - entries) / 132u : 0u;
    if (ctx.r4.u32 >= count) {
      ctx.r3.u64 = 0;
      return;
    }

    ctx.r3.u64 = entries + ctx.r4.u32 * 132u + 108;
    return;
  }

  const auto entries = LoadGuestU32(base, ctx.r3.u32 + 696);
  if (entries == 0) {
    return;
  }

  PPCRegister temp{};
  temp.f32 = float(ctx.f1.f64);
  StoreGuestU32(base, entries + ctx.r4.u32 * 132u + 20, temp.u32);
}

REX_HOOK_RAW(ChodEnemySlot784NameGetter) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto entries = LoadGuestU32(base, ctx.r3.u32 + 712);
  if (entries == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x822411F4u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x310 redirected to legacy "
                  "second-entry name getter at caller=0x{:08X}",
                  caller);
    }

    const auto end = LoadGuestU32(base, ctx.r3.u32 + 716);
    const auto count = end >= entries ? (end - entries) / 132u : 0u;
    if (ctx.r4.u32 >= count) {
      ctx.r3.u64 = 0;
      return;
    }

    ctx.r3.u64 = entries + ctx.r4.u32 * 132u + 108;
    return;
  }

  StoreGuestU16(base, entries + ctx.r4.u32 * 132u + 104, 0xFFFFu);
}

REX_HOOK_RAW(ChodEnemyPsBeelSlot808ActiveGetter) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto entries = LoadGuestU32(base, ctx.r3.u32 + 712);
  if (entries == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x822411C0u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: PsBeel vtable slot +0x328 redirected to "
                  "legacy second-entry active getter at caller=0x{:08X}",
                  caller);
    }

    const auto end = LoadGuestU32(base, ctx.r3.u32 + 716);
    const auto count = end >= entries ? (end - entries) / 132u : 0u;
    if (ctx.r4.u32 >= count) {
      ctx.r3.u64 = 0;
      return;
    }
  }

  ctx.r3.u64 = LoadGuestU8(base, entries + ctx.r4.u32 * 132u + 102);
}

REX_HOOK_RAW(ChodEnemySlot808ActiveGetter) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto entries = LoadGuestU32(base, ctx.r3.u32 + 712);
  if (entries == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x822411C0u) {
    static std::atomic_bool logged{false};
    if (!logged.exchange(true)) {
      REXLOG_INFO("HoD enemy DLL: vtable slot +0x328 redirected to legacy "
                  "second-entry active getter at caller=0x{:08X}",
                  caller);
    }

    const auto end = LoadGuestU32(base, ctx.r3.u32 + 716);
    const auto count = end >= entries ? (end - entries) / 132u : 0u;
    if (ctx.r4.u32 >= count) {
      ctx.r3.u64 = 0;
      return;
    }

    ctx.r3.u64 = LoadGuestU8(base, entries + ctx.r4.u32 * 132u + 102);
    return;
  }

  ctx.r3.u64 = entries + ctx.r4.u32 * 132u + 108;
}

REX_HOOK_RAW(ChodEnemySlot832CountGetter) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  const auto caller = static_cast<uint32_t>(ctx.lr);
  if (caller == 0x82241180u || caller == 0x82241768u) {
    const auto begin = LoadGuestU32(base, ctx.r3.u32 + 712);
    const auto end = LoadGuestU32(base, ctx.r3.u32 + 716);
    const auto valid_range = begin != 0 && end >= begin;
    const auto byte_count = valid_range ? end - begin : 0u;
    const auto count = valid_range ? byte_count / 132u : 0u;
    static std::atomic<uint32_t> detail_logs{0};
    const auto log_index = detail_logs.fetch_add(1);
    if (log_index < 64 || !valid_range || count > 256u || (byte_count % 132u) != 0) {
      REXLOG_INFO(
          "HoD enemy DLL: vtable slot +0x340 legacy count getter "
          "caller=0x{:08X} object=0x{:08X} begin=0x{:08X} end=0x{:08X} "
          "bytes=0x{:X} count={} valid={} aligned={}",
          caller, ctx.r3.u32, begin, end, byte_count, count, valid_range,
          (byte_count % 132u) == 0);
    }
    if (!valid_range) {
      ctx.r3.u64 = 0;
      return;
    }

    ctx.r3.u64 = count;
    return;
  }

  const auto entries = LoadGuestU32(base, ctx.r3.u32 + 712);
  if (entries == 0) {
    return;
  }

  const auto offset = static_cast<uint32_t>(ctx.r4.u32 * 132u);
  StoreGuestU32(base, entries + offset, ctx.r5.u32);
}

void InstallChodLooseContentHooks(rex::Runtime* runtime) {
  if (!runtime) {
    return;
  }

  if (auto* dispatcher = runtime->function_dispatcher()) {
    if (!dispatcher->SetFunction(0x828559C4, ChodXexLoadImage)) {
      REXLOG_ERROR("HoD loose content: failed to register XexLoadImage duplicate-load shim");
    }

    if (!dispatcher->SetFunction(0x8238A1E8, sub_8238A1E8)) {
      REXLOG_ERROR("HoD loose content: failed to register callback stub 0x8238A1E8");
    }

    if constexpr (kEnableAllocatorDiagnostics) {
      if (!dispatcher->SetFunction(0x82249368, ChodResourceObjectAllocTrace)) {
        REXLOG_ERROR("HoD loose content: failed to register resource object trace 0x82249368");
      }

      if (!dispatcher->SetFunction(0x825DD4C8, ChodHeapTreeAllocTrace)) {
        REXLOG_ERROR("HoD loose content: failed to register heap allocator trace 0x825DD4C8");
      }

      if (!dispatcher->SetFunction(0x825DB208, ChodBackendAllocTrace)) {
        REXLOG_ERROR("HoD loose content: failed to register backend allocator trace 0x825DB208");
      }

      if (!dispatcher->SetFunction(0x825E02A0, ChodHeapAllocWrapperTrace)) {
        REXLOG_ERROR("HoD loose content: failed to register heap wrapper trace 0x825E02A0");
      }

      if (!dispatcher->SetFunction(0x8246CBA8, ChodGameAllocCallbackTrace)) {
        REXLOG_ERROR("HoD loose content: failed to register game alloc callback trace 0x8246CBA8");
      }

      if (!dispatcher->SetFunction(0x82245760, ChodStageEnemyLegacyListTrace)) {
        REXLOG_ERROR("HoD loose content: failed to register stage enemy list trace 0x82245760");
      }
    }
  }

  runtime->kernel_state()->SetUserExportResolver(ResolveChodUserExport);
  RegisterLooseDataRootAliases();

  g_game_data_root = runtime->game_data_root();
  const auto discovered_package_ids = DiscoverLoosePackageIds(runtime->game_data_root());
  {
    const std::lock_guard lock(g_loose_content_mutex);
    g_loose_package_ids = discovered_package_ids;
  }
  if (discovered_package_ids.empty()) {
    REXLOG_INFO("HoD loose content: no loose DLC-style file names detected");
    return;
  }

  REXLOG_INFO("HoD loose content: detected {} loose DLC-style ids",
              discovered_package_ids.size());
}
