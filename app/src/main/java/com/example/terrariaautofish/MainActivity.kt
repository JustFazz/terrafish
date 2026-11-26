package com.example.terrariaautofish

import android.annotation.SuppressLint
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private lateinit var etPackage: EditText
    private lateinit var etMainTypeInfo: EditText
    private lateinit var etProjField: EditText
    private lateinit var etAiStyle: EditText
    private lateinit var etAiArray: EditText
    private lateinit var etLocalAiArray: EditText

    @SuppressLint("SetTextI18n")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val currentConfig = OffsetConfig.load(this)

        val scrollView = ScrollView(this)
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(44, 48, 44, 48)
        }

        val header = TextView(this).apply {
            text = "Terraria Auto-Fish (Root Memory Reader)\n\n" +
                   "Membaca langsung memori Terraria.Projectile (aiStyle = 61) di RAM via Root Daemon.\n" +
                   "• Tidak butuh izin rekam layar (MediaProjection)\n" +
                   "• Mendukung filter Crate otomatis dari RAM (localAI[1])\n" +
                   "• Isi Main_TypeInfo RVA dengan 0x0 untuk Auto Heap Scan"
            textSize = 14f
        }

        etPackage = createField("Package Name Terraria:", currentConfig.packageName, layout)
        etMainTypeInfo = createField("Main_TypeInfo RVA (0x0 = Auto Scan):", currentConfig.mainTypeInfoHex, layout)
        etProjField = createField("Main.projectile Static Offset:", currentConfig.projectileFieldHex, layout)
        etAiStyle = createField("Projectile.aiStyle Offset:", currentConfig.projAiStyleHex, layout)
        etAiArray = createField("Projectile.ai[] Offset:", currentConfig.projAiArrayHex, layout)
        etLocalAiArray = createField("Projectile.localAI[] Offset:", currentConfig.projLocalAiArrayHex, layout)

        val btnSave = Button(this).apply {
            text = "Simpan Konfigurasi Offset"
            setOnClickListener {
                saveCurrentOffsets()
                Toast.makeText(this@MainActivity, "Offset tersimpan", Toast.LENGTH_SHORT).show()
            }
        }

        val btnStartOverlay = Button(this).apply {
            text = "Aktifkan Floating UI & Memory Reader"
            setOnClickListener {
                saveCurrentOffsets()
                launchFloatingService()
            }
        }

        layout.addView(header, 0)
        layout.addView(btnSave)
        layout.addView(btnStartOverlay)
        scrollView.addView(layout)
        setContentView(scrollView)
    }

    private fun createField(label: String, defaultValue: String, parent: LinearLayout): EditText {
        val tv = TextView(this).apply {
            text = label
            textSize = 12f
            setPadding(0, 18, 0, 4)
        }
        val et = EditText(this).apply {
            setText(defaultValue)
            textSize = 13f
        }
        parent.addView(tv)
        parent.addView(et)
        return et
    }

    private fun saveCurrentOffsets() {
        val cfg = OffsetConfig(
            packageName = etPackage.text.toString(),
            mainTypeInfoHex = etMainTypeInfo.text.toString(),
            projectileFieldHex = etProjField.text.toString(),
            projAiStyleHex = etAiStyle.text.toString(),
            projAiArrayHex = etAiArray.text.toString(),
            projLocalAiArrayHex = etLocalAiArray.text.toString()
        )
        OffsetConfig.save(this, cfg)
    }

    private fun launchFloatingService() {
        if (!RootDaemonBridge.verifyRootAvailable()) {
            Toast.makeText(this, "Akses Root (UID 0) gagal! Berikan izin Root di Magisk/KernelSU.", Toast.LENGTH_LONG).show()
            return
        }

        if (!Settings.canDrawOverlays(this)) {
            val intent = Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:$packageName")
            )
            startActivity(intent)
            Toast.makeText(this, "Aktifkan izin Display Over Other Apps terlebih dahulu", Toast.LENGTH_SHORT).show()
            return
        }

        startForegroundService(Intent(this, FloatingFishService::class.java))
        finish()
    }
}
