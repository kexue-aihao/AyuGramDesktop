// This is the source code of AyuGram for Desktop.
#pragma once

namespace Ayu {

#ifdef TDESKTOP_SELF_BUILD_TAG
inline constexpr auto kVersion = "Ayugram Desktop " TDESKTOP_SELF_BUILD_TAG;
inline constexpr auto kReleaseTag = TDESKTOP_SELF_BUILD_TAG;
#else // TDESKTOP_SELF_BUILD_TAG
inline constexpr auto kVersion = "Ayugram Desktop v20261020";
inline constexpr auto kReleaseTag = "v20261020";
#endif // TDESKTOP_SELF_BUILD_TAG
inline constexpr auto kReleaseRepository = "kexue-aihao/AyuGramDesktop";

} // namespace Ayu
