package com.example.terrariaautofish

import android.annotation.SuppressLint
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.Color
import android.graphics.PixelFormat
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.app.NotificationCompat

class FloatingFishService : Service() {

    private lateinit var windowManager: WindowManager
    private lateinit var controlView: LinearLayout
    private lateinit var tapTargetView: TextView
    private lateinit var controlParams: WindowManager.LayoutParams
    private lateinit var tapTargetParams: WindowManager.LayoutParams

    private lateinit var tvPidStatus: TextView
    private lateinit var tvTelemetry: TextView
    private lateinit var btnStartStop: Button
    private lateinit var cbCrateOnly: CheckBox

    @Volatile
    private var isAutoFishing = false

    @Volatile
    private var crateOnlyMode = false

    private var lastActionTime = 0L
    private var waitingForCastSettle = false
    private var catchCount = 0

    private val mainHandler = Handler(Looper.getMainLooper())

    companion object {
        private const val CHANNEL_ID = "TerrariaMemAutoFishChannel"
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        windowManager = getSystemService(WINDOW_SERVICE) as WindowManager
        startForegroundServiceNotification()
        setupFloatingUI()
        startMemoryReader()
    }

    private fun startForegroundServiceNotification() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Terraria Memory AutoFish",
            NotificationManager.IMPORTANCE_LOW
        )
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)

        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Terraria AutoFish (Memory Mode)")
            .setContentText("Root Memory Daemon aktif")
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .build()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(202, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(202, notification)
        }
    }

    @SuppressLint("SetTextI18n", "ClickableViewAccessibility")
    private fun setupFloatingUI() {
        val overlayType = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        val density = resources.displayMetrics.density

        // 1. Panel Kontrol Floating
        controlView = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(26, 20, 26, 20)
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#D912161F"))
                cornerRadius = 24f
                setStroke(2, Color.parseColor("#00E676"))
            }
        }

        tvPidStatus = TextView(this).apply {
            text = "PID: Mencari Terraria..."
            setTextColor(Color.parseColor("#80CBC4"))
            textSize = 11f
        }

        tvTelemetry = TextView(this).apply {
            text = "Bobber: IDLE | Tangkapan: 0"
            setTextColor(Color.WHITE)
            textSize = 12f
            setPadding(0, 4, 0, 10)
        }

        btnStartStop = Button(this).apply {
            text = "START AUTO-FISH"
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#2E7D32"))
            textSize = 12f
            setOnClickListener { toggleFishing() }
        }

        cbCrateOnly = CheckBox(this).apply {
            text = "Crate Only (Filter RAM)"
            setTextColor(Color.parseColor("#FFD54F"))
            textSize = 11f
            setOnCheckedChangeListener { _, isChecked ->
                crateOnlyMode = isChecked
            }
        }

        val btnClose = TextView(this).apply {
            text = "✕ Tutup Floating UI"
            setTextColor(Color.parseColor("#FF5252"))
            textSize = 11f
            setPadding(0, 8, 0, 0)
            setOnClickListener { stopSelf() }
        }

        controlView.addView(tvPidStatus)
        controlView.addView(tvTelemetry)
        controlView.addView(btnStartStop)
        controlView.addView(cbCrateOnly)
        controlView.addView(btnClose)

        controlParams = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            overlayType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 50
            y = 100
        }

        // 2. Titik Tap Lempar Kail (Geser ke area air agar tidak menyentuh tombol HUD)
        val markerSize = (36 * density).toInt()
        tapTargetView = TextView(this).apply {
            text = "TAP"
            gravity = Gravity.CENTER
            textSize = 9f
            setTextColor(Color.WHITE)
            background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(Color.parseColor("#5500B0FF"))
                setStroke(3, Color.parseColor("#00B0FF"))
            }
        }

        tapTargetParams = WindowManager.LayoutParams(
            markerSize,
            markerSize,
            overlayType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 550
            y = 350
        }

        makeDraggable(controlView, controlParams)
        makeDraggable(tapTargetView, tapTargetParams)

        windowManager.addView(controlView, controlParams)
        windowManager.addView(tapTargetView, tapTargetParams)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun makeDraggable(view: View, params: WindowManager.LayoutParams) {
        var initX = 0
        var initY = 0
        var touchX = 0f
        var touchY = 0f

        view.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    initX = params.x
                    initY = params.y
                    touchX = event.rawX
                    touchY = event.rawY
                    false
                }
                MotionEvent.ACTION_MOVE -> {
                    params.x = initX + (event.rawX - touchX).toInt()
                    params.y = initY + (event.rawY - touchY).toInt()
                    windowManager.updateViewLayout(view, params)
                    true
                }
                else -> false
            }
        }
    }

    @SuppressLint("SetTextI18n")
    private fun toggleFishing() {
        isAutoFishing = !isAutoFishing
        if (isAutoFishing) {
            btnStartStop.text = "STOP AUTO-FISH"
            btnStartStop.setBackgroundColor(Color.parseColor("#C62828"))
            lastActionTime = 0L
            waitingForCastSettle = false
        } else {
            btnStartStop.text = "START AUTO-FISH"
            btnStartStop.setBackgroundColor(Color.parseColor("#2E7D32"))
        }
    }

    private fun startMemoryReader() {
        val config = OffsetConfig.load(this)
        RootDaemonBridge.startDaemon(
            context = this,
            config = config,
            onTelemetry = { telemetry ->
                handleTelemetry(telemetry)
            },
            onError = { errMsg ->
                mainHandler.post {
                    tvPidStatus.text = errMsg
                }
            }
        )
    }

    @SuppressLint("SetTextI18n")
    private fun handleTelemetry(t: BobberTelemetry) {
        val now = SystemClock.uptimeMillis()

        mainHandler.post {
            if (t.pid <= 0) {
                tvPidStatus.text = "PID: Game belum berjalan"
            } else {
                tvPidStatus.text = "PID: ${t.pid} | il2cpp: 0x${t.il2cppBaseHex}"
            }

            if (t.bobberFound) {
                val itemInfo = TerrariaItemIds.getDisplayName(t.hookedItemId)
                tvTelemetry.text = "ai[1]=${t.ai1BiteTimer} | $itemInfo | Total:$catchCount"
            } else {
                tvTelemetry.text = "Bobber: Tidak ada di air | Total:$catchCount"
            }
        }

        if (!isAutoFishing || t.pid <= 0) return

        val loc = IntArray(2)
        tapTargetView.getLocationOnScreen(loc)
        val tapX = loc[0] + (tapTargetView.width / 2)
        val tapY = loc[1] + (tapTargetView.height / 2)

        // 1. Jika tidak ada bobber di air, lempar kail otomatis
        if (!t.bobberFound) {
            if (now - lastActionTime > 1200L) {
                RootDaemonBridge.sendTap(tapX, tapY)
                lastActionTime = now
                waitingForCastSettle = true
            }
            return
        }

        if (waitingForCastSettle && now - lastActionTime < 1000L) {
            return
        }
        waitingForCastSettle = false

        // 2. Cek apakah ikan menyambar: ai[1] < 0f menandakan gigitan aktif di RAM
        if (t.ai0State == 0f && t.ai1BiteTimer < -0.5f && now - lastActionTime > 900L) {
            if (crateOnlyMode) {
                // Mode Filter Crate: abaikan ikan biasa, biarkan lepas sendiri tanpa membuang umpan
                if (TerrariaItemIds.isCrate(t.hookedItemId)) {
                    catchCount++
                    RootDaemonBridge.sendTap(tapX, tapY)
                    lastActionTime = now
                }
            } else {
                catchCount++
                RootDaemonBridge.sendTap(tapX, tapY)
                lastActionTime = now
            }
        }
    }

    override fun onDestroy() {
        isAutoFishing = false
        RootDaemonBridge.stopDaemon()
        if (::controlView.isInitialized) windowManager.removeView(controlView)
        if (::tapTargetView.isInitialized) windowManager.removeView(tapTargetView)
        super.onDestroy()
    }
}
