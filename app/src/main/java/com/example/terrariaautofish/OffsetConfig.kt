package com.example.terrariaautofish

import android.content.Context

data class OffsetConfig(
    val packageName: String = "com.and.games505.TerrariaPaid",
    val mainTypeInfoHex: String = "0x0",       // 0x0 = Auto Heap Scan (instan dengan cache alamat bobber)
    val projectileFieldHex: String = "0x5B8",
    val projAiStyleHex: String = "0x98",
    val projAiArrayHex: String = "0x78",
    val projLocalAiArrayHex: String = "0x84"
) {
    companion object {
        private const val PREFS_NAME = "terraria_mem_offsets"

        fun load(context: Context): OffsetConfig {
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            return OffsetConfig(
                packageName = prefs.getString("packageName", "com.and.games505.TerrariaPaid")!!,
                mainTypeInfoHex = prefs.getString("mainTypeInfoHex", "0x0")!!,
                projectileFieldHex = prefs.getString("projectileFieldHex", "0x168")!!,
                projAiStyleHex = prefs.getString("projAiStyleHex", "0x94")!!,
                projAiArrayHex = prefs.getString("projAiArrayHex", "0xB8")!!,
                projLocalAiArrayHex = prefs.getString("projLocalAiArrayHex", "0xC0")!!
            )
        }

        fun save(context: Context, config: OffsetConfig) {
            context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
                .putString("packageName", config.packageName.trim())
                .putString("mainTypeInfoHex", config.mainTypeInfoHex.trim())
                .putString("projectileFieldHex", config.projectileFieldHex.trim())
                .putString("projAiStyleHex", config.projAiStyleHex.trim())
                .putString("projAiArrayHex", config.projAiArrayHex.trim())
                .putString("projLocalAiArrayHex", config.projLocalAiArrayHex.trim())
                .apply()
        }
    }
}
