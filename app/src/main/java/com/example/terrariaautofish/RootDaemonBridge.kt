package com.example.terrariaautofish

import android.content.Context
import java.io.BufferedReader
import java.io.DataOutputStream
import java.io.File
import java.io.InputStreamReader

data class BobberTelemetry(
    val pid: Int,
    val il2cppBaseHex: String,
    val bobberFound: Boolean,
    val ai0State: Float,
    val ai1BiteTimer: Float,
    val hookedItemId: Int
)

object RootDaemonBridge {
    private var rootProcess: Process? = null
    private var stdinStream: DataOutputStream? = null
    private var readerThread: Thread? = null

    @Volatile
    var isConnected: Boolean = false
        private set

    fun verifyRootAvailable(): Boolean {
        return try {
            val proc = Runtime.getRuntime().exec(arrayOf("su", "-c", "id"))
            val out = BufferedReader(InputStreamReader(proc.inputStream)).readLine() ?: ""
            proc.waitFor()
            out.contains("uid=0")
        } catch (e: Exception) {
            false
        }
    }

    fun startDaemon(
        context: Context,
        config: OffsetConfig,
        onTelemetry: (BobberTelemetry) -> Unit,
        onError: (String) -> Unit
    ) {
        stopDaemon()

        val binaryPath = "${context.applicationInfo.nativeLibraryDir}/libmemreader.so"
        val binaryFile = File(binaryPath)
        if (!binaryFile.exists()) {
            onError("Binary native tidak ditemukan: $binaryPath")
            return
        }

        try {
            val cmd = listOf(
                binaryPath,
                config.packageName,
                config.mainTypeInfoHex,
                config.projectileFieldHex,
                config.projAiStyleHex,
                config.projAiArrayHex,
                config.projLocalAiArrayHex
            ).joinToString(" ")

            rootProcess = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
            stdinStream = DataOutputStream(rootProcess!!.outputStream)
            isConnected = true

            readerThread = Thread {
                try {
                    val stdout = BufferedReader(InputStreamReader(rootProcess!!.inputStream))
                    while (isConnected) {
                        val line = stdout.readLine() ?: break
                        val parts = line.split("|")
                        if (parts.size == 7 && parts[0] == "STATUS") {
                            val telemetry = BobberTelemetry(
                                pid = parts[1].toIntOrNull() ?: 0,
                                il2cppBaseHex = parts[2],
                                bobberFound = parts[3] == "1",
                                ai0State = parts[4].toFloatOrNull() ?: 0f,
                                ai1BiteTimer = parts[5].toFloatOrNull() ?: 0f,
                                hookedItemId = parts[6].toIntOrNull() ?: 0
                            )
                            onTelemetry(telemetry)
                        }
                    }
                } catch (e: Exception) {
                    if (isConnected) {
                        onError("Koneksi daemon terputus: ${e.message}")
                    }
                } finally {
                    isConnected = false
                }
            }.apply { start() }
        } catch (e: Exception) {
            isConnected = false
            onError("Gagal menjalankan root daemon: ${e.message}")
        }
    }

    fun sendTap(x: Int, y: Int) {
        try {
            stdinStream?.apply {
                writeBytes("TAP $x $y\n")
                flush()
            }
        } catch (_: Exception) {
        }
    }

    fun stopDaemon() {
        isConnected = false
        try {
            stdinStream?.writeBytes("QUIT\n")
            stdinStream?.flush()
            stdinStream?.close()
        } catch (_: Exception) {
        }
        try {
            rootProcess?.destroy()
        } catch (_: Exception) {
        }
        stdinStream = null
        rootProcess = null
        readerThread = null
    }
}
