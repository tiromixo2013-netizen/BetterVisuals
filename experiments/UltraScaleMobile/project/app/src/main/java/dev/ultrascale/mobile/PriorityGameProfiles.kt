package dev.ultrascale.mobile

/**
 * Research profiles for the three priority games.
 *
 * These profiles are deliberately separate from GameAdapterRegistry's executable
 * routes: a fingerprint candidate is not an enabled injection adapter.
 */
enum class GameProfileState {
    IDENTITY_OBSERVED,
    RESEARCH_ONLY,
    BLOCKED_BY_PLATFORM,
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
    val integrationNotes: String
) {
    fun matches(packageId: String?, version: String?, abis: Set<String>): Boolean {
        if (packageId != packageName || abis.isEmpty() || abis.intersect(candidateAbis).isEmpty()) return false
        return versionPrefixes.isEmpty() || versionPrefixes.any { version?.startsWith(it) == true }
    }
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
            integrationNotes = "User-selected APK identity was read as 9.1.0i (91009), but scanner reported ABI none and layer eligibility blocked. Inspect every split before ABI matching; no game render adapter is implemented."
        ),
        GameResearchProfile(
            id = "geometry-dash-2081-2091",
            title = "Geometry Dash",
            packageName = "com.robtopx.geometryjump",
            versionPrefixes = setOf("2.2081", "2.2091"),
            candidateAbis = setOf("arm64-v8a"),
            apiCandidates = setOf("OpenGL ES"),
            state = GameProfileState.RESEARCH_ONLY,
            sharedSettingsUiAcrossVersions = true,
            integrationNotes = "Keep one shared settings-integration design, but fingerprint 2.2081 and 2.2091 independently. The existing Geode route is a mod-import route, not proof of external API-layer injection or a completed UltraScale render pass."
        ),
        GameResearchProfile(
            id = "beach-buggy-racing",
            title = "Beach Buggy Racing",
            packageName = "com.vectorunit.purple.googleplay",
            versionPrefixes = emptySet(),
            candidateAbis = arm32And64,
            apiCandidates = setOf("OpenGL ES", "Vulkan"),
            state = GameProfileState.RESEARCH_ONLY,
            integrationNotes = "Original Beach Buggy Racing package candidate only. Inspect the complete base/split set and native engine libraries; do not match Beach Buggy Racing 2 or other store variants by display name."
        )
    )

    fun identify(packageName: String?): GameResearchProfile? =
        all.singleOrNull { it.packageName == packageName }

    fun match(packageName: String?, versionName: String?, abis: Set<String>): GameResearchProfile? =
        all.singleOrNull { it.matches(packageName, versionName, abis) }
}
