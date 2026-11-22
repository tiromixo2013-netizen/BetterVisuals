package dev.ultrascale.mobile

/**
 * Research and integration state for the three priority games.
 *
 * SOURCE_RENDER_PASS_IMPLEMENTED means the repository contains an in-process render
 * pass for the pinned game version. It is not a claim of successful runtime testing
 * on every device or game build.
 */
enum class GameProfileState {
    IDENTITY_OBSERVED,
    RESEARCH_ONLY,
    BLOCKED_BY_PLATFORM,
    SOURCE_RENDER_PASS_IMPLEMENTED,
    RUNTIME_LAYER_TESTED,
    GAME_RENDER_PASS_TESTED
}

data class GameResearchProfile(
    val id: String,
    val title: String,
    val packageName: String,
    val versionPrefixes: Set<String>,
    val candidateAbis: Set<String>,
    val apiCandidates: Set<String>,
    val state: GameProfileState,
    val sharedSettingsUiAcrossVersions: Boolean = false,
    val renderPassVersions: Set<String> = emptySet(),
    val integrationNotes: String
) {
    fun matches(packageId: String?, version: String?, abis: Set<String>): Boolean {
        if (packageId != packageName || abis.isEmpty() || abis.intersect(candidateAbis).isEmpty()) return false
        return versionPrefixes.isEmpty() || versionPrefixes.any { version?.startsWith(it) == true }
    }

    fun hasSourceRenderPass(version: String?): Boolean =
        version != null && renderPassVersions.any { version.startsWith(it) }
}

object PriorityGameProfiles {
    private val arm32And64 = setOf("arm64-v8a", "armeabi-v7a")

    val all: List<GameResearchProfile> = listOf(
        GameResearchProfile(
            id = "asphalt8-91009",
            title = "Asphalt 8",
            packageName = "com.gameloft.android.ANMP.GloftA8HM",
            versionPrefixes = setOf("9.1.0i"),
            candidateAbis = arm32And64,
            apiCandidates = setOf("OpenGL ES", "Vulkan"),
            state = GameProfileState.BLOCKED_BY_PLATFORM,
            integrationNotes = "The inspected 9.1.0i (91009) base APK had no native ABI entries and the installed target was not eligible for external graphics layers. Inspect the complete installed split set and determine the active renderer before designing an engine adapter. No Asphalt render-pass adapter exists."
        ),
        GameResearchProfile(
            id = "geometry-dash-2081-2091",
            title = "Geometry Dash",
            packageName = "com.robtopx.geometryjump",
            versionPrefixes = setOf("2.2081", "2.2091"),
            candidateAbis = setOf("arm64-v8a"),
            apiCandidates = setOf("OpenGL ES"),
            state = GameProfileState.SOURCE_RENDER_PASS_IMPLEMENTED,
            sharedSettingsUiAcrossVersions = true,
            renderPassVersions = setOf("2.2081"),
            integrationNotes = "The repository already contains an in-process Geode/Cocos2d OpenGL ES render pass with off-screen scene capture, Render Scale, FSR1 upscaling and CAS. The Geode manifest and current adapter route are pinned to Android 2.2081 ARM64. Version 2.2091 is recognized for shared settings design only; it is not covered by this binary adapter until rebuilt and compatibility-tested. Source implementation is not device runtime proof."
        ),
        GameResearchProfile(
            id = "beach-buggy-racing",
            title = "Beach Buggy Racing",
            packageName = "com.vectorunit.purple.googleplay",
            versionPrefixes = emptySet(),
            candidateAbis = arm32And64,
            apiCandidates = setOf("OpenGL ES", "Vulkan"),
            state = GameProfileState.RESEARCH_ONLY,
            integrationNotes = "Original Beach Buggy Racing package candidate only. Inspect installed base and split APKs to identify native engine libraries and the active renderer. No game-specific render-pass adapter exists; do not match Beach Buggy Racing 2 or other store variants by display name."
        )
    )

    fun identify(packageName: String?): GameResearchProfile? =
        all.singleOrNull { it.packageName == packageName }

    fun match(packageName: String?, versionName: String?, abis: Set<String>): GameResearchProfile? =
        all.singleOrNull { it.matches(packageName, versionName, abis) }
}
