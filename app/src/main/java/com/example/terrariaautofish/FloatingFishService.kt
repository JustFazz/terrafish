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
import kotlin.math.abs

class FloatingFishService : Service() {

    private lateinit var windowManager: WindowManager
    private lateinit var controlView: LinearLayout
    private lateinit var minimizedBubbleView: TextView
    private lateinit var tapTargetView: TextView

    private lateinit var controlParams: WindowManager.LayoutParams
    private lateinit var bubbleParams: WindowManager.LayoutParams
    private lateinit var tapTargetParams: WindowManager.LayoutParams

    private lateinit var tvPidStatus: TextView
    private lateinit var tvTelemetry: TextView
    private lateinit var btnStartStop: Button
    private lateinit var btnToggleFloating: Button
    private lateinit var cbCrateOnly: CheckBox

    @Volatile
    private var isAutoFishing = false

    @Volatile
    private var isFloatingEnabled = true

    @Volatile
    private var crateOnlyMode = false

    private var isMinimized = false
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

        // 1. Panel Kontrol Floating (Expanded)
        controlView = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 18, 24, 18)
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#E612161F"))
                cornerRadius = 24f
                setStroke(2, Color.parseColor("#00E676"))
            }
        }

        val topBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }

        tvPidStatus = TextView(this).apply {
            text = "PID: Mencari Terraria..."
            setTextColor(Color.parseColor("#80CBC4"))
            textSize = 11f
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }

        val btnMinimize = TextView(this).apply {
            text = "  —  "
            setTextColor(Color.parseColor("#00E5FF"))
            textSize = 15f
            setPadding(12, 0, 4, 0)
            setOnClickListener { setMinimizedState(true) }
        }

        topBar.addView(tvPidStatus)
        topBar.addView(btnMinimize)

        tvTelemetry = TextView(this).apply {
            text = "Bobber: IDLE | Total: 0"
            setTextColor(Color.WHITE)
            textSize = 12f
            setPadding(0, 4, 0, 8)
        }

        btnStartStop = Button(this).apply {
            text = "START AUTO-FISH"
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#2E7D32"))
            textSize = 12f
            setOnClickListener { toggleFishing() }
        }

        btnToggleFloating = Button(this).apply {
            text = "FLOATING: ON"
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#1565C0"))
            textSize = 11f
            setOnClickListener { toggleFloatingOverlay() }
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
            text = "✕ Tutup Service"
            setTextColor(Color.parseColor("#FF5252"))
            textSize = 11f
            setPadding(0, 6, 0, 0)
            setOnClickListener { stopSelf() }
        }

        controlView.addView(topBar)
        controlView.addView(tvTelemetry)
        controlView.addView(btnStartStop)
        controlView.addView(btnToggleFloating)
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
            x = 40
            y = 90
        }

        // 2. Tombol Bulat Minimize (Bubble)
        val bubbleSize = (48 * density).toInt()
        minimizedBubbleView = TextView(this).apply {
            text = "FISH"
            gravity = Gravity.CENTER
            textSize = 10f
            setTextColor(Color.WHITE)
            visibility = View.GONE
            background = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(Color.parseColor("#D912161F"))
                setStroke(3, Color.parseColor("#00E676"))
            }
        }

        bubbleParams = WindowManager.LayoutParams(
            bubbleSize,
            bubbleSize,
            overlayType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 40
            y = 90
        }

        // 3. Titik Tap Lempar Kail (Floating Target)
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

        makeDraggable(controlView, controlParams, null)
        makeDraggable(minimizedBubbleView, bubbleParams) {
            setMinimizedState(false)
        }
        makeDraggable(tapTargetView, tapTargetParams, null)

        windowManager.addView(controlView, controlParams)
        windowManager.addView(minimizedBubbleView, bubbleParams)
        windowManager.addView(tapTargetView, tapTargetParams)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun makeDraggable(
        view: View,
        params: WindowManager.LayoutParams,
        onClickAction: (() -> Unit)?
    ) {
        var initX = 0
        var initY = 0
        var touchX = 0f
        var touchY = 0f
        var moved = false

        view.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    initX = params.x
                    initY = params.y
                    touchX = event.rawX
                    touchY = event.rawY
                    moved = false
                    false
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = event.rawX - touchX
                    val dy = event.rawY - touchY
                    if (abs(dx) > 10f || abs(dy) > 10f) {
                        moved = true
                    }
                    params.x = initX + dx.toInt()
                    params.y = initY + dy.toInt()
                    windowManager.updateViewLayout(view, params)
                    true
                }
                MotionEvent.ACTION_UP -> {
                    if (!moved && onClickAction != null) {
                        onClickAction.invoke()
                        true
                    } else {
                        false
                    }
                }
                else -> false
            }
        }
    }

    private fun setMinimizedState(minimize: Boolean) {
        isMinimized = minimize
        if (minimize) {
            bubbleParams.x = controlParams.x
            bubbleParams.y = controlParams.y
            windowManager.updateViewLayout(minimizedBubbleView, bubbleParams)
            controlView.visibility = View.GONE
            minimizedBubbleView.visibility = View.VISIBLE
            updateBubbleAppearance()
        } else {
            controlParams.x = bubbleParams.x
            controlParams.y = bubbleParams.y
            windowManager.updateViewLayout(controlView, controlParams)
            minimizedBubbleView.visibility = View.GONE
            controlView.visibility = View.VISIBLE
        }
    }

    private fun updateBubbleAppearance() {
        val strokeColor = when {
            isAutoFishing -> Color.parseColor("#00E676")
            isFloatingEnabled -> Color.parseColor("#00B0FF")
            else -> Color.parseColor("#90A4AE")
        }
        minimizedBubbleView.background = GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(Color.parseColor("#E612161F"))
            setStroke(3, strokeColor)
        }
    }

    @SuppressLint("SetTextI18n")
    private fun toggleFloatingOverlay() {
        isFloatingEnabled = !isFloatingEnabled
        if (isFloatingEnabled) {
            btnToggleFloating.text = "FLOATING: ON"
            btnToggleFloating.setBackgroundColor(Color.parseColor("#1565C0"))
            tapTargetView.visibility = View.VISIBLE
        } else {
            // Matikan auto-fish saat floating dinonaktifkan agar tap tidak menyasar sembarangan
            if (isAutoFishing) {
                stopAutoFishing()
            }
            btnToggleFloating.text = "FLOATING: OFF"
            btnToggleFloating.setBackgroundColor(Color.parseColor("#455A64"))
            tapTargetView.visibility = View.GONE
        }
        updateBubbleAppearance()
    }

    @SuppressLint("SetTextI18n")
    private fun toggleFishing() {
        if (!isAutoFishing) {
            if (!isFloatingEnabled) {
                isFloatingEnabled = true
                btnToggleFloating.text = "FLOATING: ON"
                btnToggleFloating.setBackgroundColor(Color.parseColor("#1565C0"))
                tapTargetView.visibility = View.VISIBLE
            }
            isAutoFishing = true
            btnStartStop.text = "STOP AUTO-FISH"
            btnStartStop.setBackgroundColor(Color.parseColor("#C62828"))
            lastActionTime = 0L
            waitingForCastSettle = false

            // Buat lingkaran TAP tembus sentuhan agar input tap langsung masuk ke Terraria
            tapTargetParams.flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
            tapTargetView.alpha = 0.45f
            windowManager.updateViewLayout(tapTargetView, tapTargetParams)
        } else {
            stopAutoFishing()
        }
        updateBubbleAppearance()
    }

    @SuppressLint("SetTextI18n")
    private fun stopAutoFishing() {
        isAutoFishing = false
        btnStartStop.text = "START AUTO-FISH"
        btnStartStop.setBackgroundColor(Color.parseColor("#2E7D32"))

        // Kembalikan agar lingkaran TAP bisa digeser lagi
        tapTargetParams.flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
        tapTargetView.alpha = 1.0f
        windowManager.updateViewLayout(tapTargetView, tapTargetParams)
        updateBubbleAppearance()
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
                tvTelemetry.text = "ai[0]=${t.ai0State} ai[1]=${t.ai1BiteTimer} | $itemInfo | Total:$catchCount"
            } else {
                tvTelemetry.text = "Bobber: Tidak ada di air | Total:$catchCount"
            }
        }

        if (!isAutoFishing || !isFloatingEnabled || t.pid <= 0) return

        val loc = IntArray(2)
        tapTargetView.getLocationOnScreen(loc)
        val tapX = loc[0] + (tapTargetView.width / 2)
        val tapY = loc[1] + (tapTargetView.height / 2)

        // 1. Jika tidak ada bobber di air, lempar kail otomatis
        if (!t.bobberFound) {
            if (now - lastActionTime > 1300L) {
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

        // 2. Deteksi ikan menyambar: ai[0] == 1.0f atau ai[1] < -0.5f
        val isBiting = (t.ai0State == 1.0f || t.ai1BiteTimer < -0.5f)
        if (isBiting && now - lastActionTime > 900L) {
            if (crateOnlyMode) {
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
        if (::minimizedBubbleView.isInitialized) windowManager.removeView(minimizedBubbleView)
        if (::tapTargetView.isInitialized) windowManager.removeView(tapTargetView)
        super.onDestroy()
    }
}
