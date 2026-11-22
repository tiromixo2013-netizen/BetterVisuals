package dev.ultrascale.mobile

/**
 * Explicit, version-scoped integration routes. A route is not an arbitrary APK patcher.
 * Geometry Dash is supported through the Geode mod built by this repository's Geode workflow.
 */
object GameAdapterRegistry {
    data class Route(
        val id: String,
        val displayName: String,
        val packageName: String,
        val gameVersion: String,
        val requiredAbi: String,
        val mechanism: String,
        val note: String
    )

    data class Match(val route: Route?, val reason: String)

    private val routes = listOf(
        Route(
            id = "geometry-dash-geode-2081-arm64",
            displayName = "Geometry Dash · Geode adapter",
            packageName = "com.robtopx.geometryjump",
            gameVersion = "2.2081",
            requiredAbi = "arm64-v8a",
            mechanism = "GEODE_MOD_IMPORT",
            note = "Uses this repository's BetterVisuals Geode mod. It installs a .geode module into the Geode mods directory; it does not rewrite the game's APK."
        )
    )

    fun resolve(result: ApkInjectionPipeline.Preflight): Match {
        if (result.decision != "ADAPTER_REQUIRED" || !result.validZip || !result.hasManifest || result.dexFiles.isEmpty()) {
            return Match(null, "APK preflight did not pass; adapter routing is blocked.")
        }
        if (result.packageName.isNullOrBlank()) {
            return Match(null, "The APK package identity could not be read.")
        }
        val route = routes.firstOrNull { it.packageName == result.packageName }
            ?: return Match(null, "No tested integration route is registered for ${result.packageName}.")
        if (result.versionName != route.gameVersion) {
            return Match(null, "Found ${route.displayName}, but this adapter is pinned to game version ${route.gameVersion}; detected ${result.versionName ?: "unknown"}.")
        }
        if (route.requiredAbi !in result.nativeAbis) {
            return Match(null, "The adapter requires ${route.requiredAbi}; detected ABIs: ${result.nativeAbis.joinToString().ifBlank { "none" }}.")
        }
        return Match(route, route.note)
    }
}
