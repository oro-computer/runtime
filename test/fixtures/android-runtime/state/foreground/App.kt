package android.app

import android.content.*
import android.os.*

class Application { companion object { fun getProcessName() = "oro.runtime.public.state" } }
class ActivityManager {
  class ProcessInfo { val pid = 1; val processName = "oro.runtime.public.state" }
  val runningAppProcesses = listOf(ProcessInfo())
}
open class Service: Context() {
  companion object { const val STOP_FOREGROUND_REMOVE = 1; const val START_NOT_STICKY = 2 }
  var destroyed = false
  private var notificationId = 0
  open fun onBind(intent: Intent?): IBinder? = null
  open fun onStartCommand(intent: Intent?, flags: Int, startId: Int) = START_NOT_STICKY
  open fun onTimeout(startId: Int, fgsType: Int) {}
  open fun onTaskRemoved(intent: Intent?) {}
  open fun onDestroy() {}
  fun startForeground(id: Int, notification: Notification) { notificationId = id; PublicEnvironment.notificationManager.active[id] = notification }
  fun startForeground(id: Int, notification: Notification, type: Int) = startForeground(id, notification)
  fun stopForeground(mode: Int) { PublicEnvironment.notificationManager.active.remove(notificationId) }
  fun stopSelf(startId: Int) = stopSelf()
  fun stopSelf() {
    if (PublicEnvironment.delayDestroy) return
    if (!destroyed) { destroyed = true; onDestroy(); if (PublicEnvironment.service === this) PublicEnvironment.service = null; if (PublicEnvironment.continuousService === this) PublicEnvironment.continuousService = null }
  }
}
class NotificationChannel(val id: String, name: String, var importance: Int)
class NotificationManager {
  companion object { const val IMPORTANCE_LOW = 2; const val IMPORTANCE_NONE = 0 }
  val active = mutableMapOf<Int, Notification>()
  val channels = mutableMapOf<String,NotificationChannel>()
  fun createNotificationChannel(channel: NotificationChannel) { channels.putIfAbsent(channel.id, channel) }
  fun getNotificationChannel(id: String) = channels[id]
  fun areNotificationsEnabled() = PublicEnvironment.notificationsAllowed
}
class PendingIntent(val context: Context, val intent: Intent) {
  companion object {
    const val FLAG_IMMUTABLE = 1; const val FLAG_UPDATE_CURRENT = 2
    fun getService(context: Context, id: Int, intent: Intent, flags: Int) = PendingIntent(context,intent)
    fun getActivity(context: Context, id: Int, intent: Intent, flags: Int) = PendingIntent(context,intent)
  }
  fun send() { (if (intent.type == oro.runtime.app.RuntimeContinuousService::class.java) PublicEnvironment.continuousService else PublicEnvironment.service)?.onStartCommand(intent,0,1) }
}
class Notification(val actions: List<Action>) {
  companion object { const val VISIBILITY_PRIVATE = 0; const val VISIBILITY_PUBLIC = 1 }
  class Action(val title: String, val actionIntent: PendingIntent) {
    class Builder(icon: Any?, val title: String, val intent: PendingIntent) { fun build() = Action(title,intent) }
  }
  class Builder(context: Context, channel: String) {
    val actions = mutableListOf<Action>()
    fun setSmallIcon(icon: Int) = this
    fun setContentTitle(title: String) = this
    fun setContentText(text: String) = this
    fun setOngoing(value: Boolean) = this
    fun setOnlyAlertOnce(value: Boolean) = this
    fun setVisibility(value: Int) = this
    fun setContentIntent(value: PendingIntent) = this
    fun addAction(value: Action) = apply { actions.add(value) }
    fun build() = Notification(actions.toList())
  }
}
