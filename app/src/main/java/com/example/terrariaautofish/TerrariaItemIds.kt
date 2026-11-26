package com.example.terrariaautofish

object TerrariaItemIds {
    private val CRATE_IDS = mapOf(
        2334 to "Wooden Crate",
        2335 to "Iron Crate",
        2336 to "Golden Crate",
        3203 to "Corrupt Crate",
        3204 to "Crimson Crate",
        3205 to "Dungeon Crate",
        3206 to "Sky Crate",
        3207 to "Hallowed Crate",
        3208 to "Jungle Crate",
        3979 to "Pearlwood Crate",
        3980 to "Mythril Crate",
        3981 to "Titanium Crate",
        3982 to "Bramble Crate",
        3983 to "Azure Crate",
        3984 to "Defiled Crate",
        3985 to "Hematic Crate",
        3986 to "Divine Crate",
        3987 to "Stockade Crate",
        4405 to "Frozen Crate",
        4406 to "Boreal Crate",
        4407 to "Oasis Crate",
        4408 to "Mirage Crate",
        4877 to "Obsidian Crate",
        4878 to "Hellstone Crate",
        5002 to "Ocean Crate",
        5003 to "Seaside Crate"
    )

    private val RARE_CATCHES = mapOf(
        2308 to "Golden Carp",
        2311 to "Specious/Prismite",
        2313 to "Obsidifish",
        2314 to "Flarefin Koi",
        2420 to "Zephyr Fish",
        3210 to "Bladetongue",
        3211 to "Crystal Serpent",
        3209 to "Toxikarp",
        4382 to "Blood Moon Trophy/Item"
    )

    fun isCrate(itemId: Int): Boolean = CRATE_IDS.containsKey(itemId)

    fun getDisplayName(itemId: Int): String {
        if (itemId <= 0) return "-"
        CRATE_IDS[itemId]?.let { return "$it (#$itemId)" }
        RARE_CATCHES[itemId]?.let { return "$it (#$itemId)" }
        return "Item #$itemId"
    }
}
