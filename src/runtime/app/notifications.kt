package oro.runtime.app

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
import android.media.AudioAttributes
import android.media.RingtoneManager
import android.net.Uri
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.graphics.drawable.IconCompat
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/** Native-only submission shares the same opaque routing/receipt ledger as WebView calls. */
object NativeNotifications {
  /** Cancels this package's exact ID/tag identity without starting an Activity. */
  fun cancel (context: Context, id: String, tag: String = ""): Boolean {
    if (id.length !in 1..256 || tag.length > 256) return false
    return try {
      val identifier = id.toLongOrNull()?.toInt() ?: id.hashCode()
      NotificationManagerCompat.from(context).cancel(NativeNotifications.tag(id, tag), identifier)
      true
    } catch (_: RuntimeException) { false }
  }

  private fun text (options: JSONObject, key: String, limit: Int, default: String = ""): String {
    if (!options.has(key)) return default
    require(options.get(key) is String)
    return options.getString(key).also { require(it.length <= limit) }
  }
  private fun flag (options: JSONObject, key: String, default: Boolean = false): Boolean {
    if (!options.has(key)) return default
    require(options.get(key) is Boolean)
    return options.getBoolean(key)
  }
  private fun resource (context: Context, name: String, type: String): Int {
    require(Regex("[a-z][a-z0-9_]{0,127}").matches(name))
    return context.resources.getIdentifier(name, type, context.packageName).also { require(it != 0) }
  }

  /** Defaults are immutable per channel ID; existing user channel settings always win. */
  @Synchronized
  fun configureChannels (context: Context, channels: JSONArray): JSONArray {
    if (!App.getInstance().hasRuntimePermission("notifications")) throw PlatformFailure("EACCES")
    require(channels.length() in 1..32)
    val manager = context.getSystemService(NotificationManager::class.java)
    val store = context.getSharedPreferences("oro.notification.channels.v1", Context.MODE_PRIVATE)
    val ids = mutableSetOf<String>()
    val normalized = (0 until channels.length()).map { index ->
      val item = channels.getJSONObject(index)
      require(item.keys().asSequence().all { it in listOf("id", "name", "description", "importance", "sound", "vibration", "badge") })
      val id = text(item, "id", 128)
      require(Regex("[a-zA-Z0-9][a-zA-Z0-9_.-]{0,127}").matches(id) && ids.add(id) && id !in listOf(context.packageName, "default"))
      val name = text(item, "name", 128)
      require(name.isNotEmpty())
      val importance = text(item, "importance", 16, "default")
      require(importance in listOf("min", "low", "default", "high"))
      val sound = text(item, "sound", 160, "default")
      require(sound in listOf("default", "none") || sound.startsWith("raw/"))
      if (sound.startsWith("raw/")) resource(context, sound.removePrefix("raw/"), "raw")
      JSONObject().put("id", id).put("name", name).put("description", text(item, "description", 512))
        .put("importance", importance).put("sound", sound).put("vibration", flag(item, "vibration"))
        .put("badge", flag(item, "badge", true))
    }
    if ((store.all.keys + ids).toSet().size > 32) throw PlatformFailure("ENOSPC")
    for (item in normalized) {
      val id = item.getString("id")
      val previous = store.getString(id, null)
      if ((previous != null && previous != item.toString()) ||
          (previous == null && manager.getNotificationChannel(id) != null)) throw PlatformFailure("EEXIST")
    }
    val result = JSONArray()
    for (item in normalized) {
      val id = item.getString("id")
      if (!store.edit().putString(id, item.toString()).commit()) throw PlatformFailure("EIO")
      if (manager.getNotificationChannel(id) == null) {
        val importance = when (item.getString("importance")) {
          "min" -> NotificationManager.IMPORTANCE_MIN
          "low" -> NotificationManager.IMPORTANCE_LOW
          "high" -> NotificationManager.IMPORTANCE_HIGH
          else -> NotificationManager.IMPORTANCE_DEFAULT
        }
        manager.createNotificationChannel(NotificationChannel(id, item.getString("name"), importance).apply {
          description = item.getString("description")
          lockscreenVisibility = NotificationCompat.VISIBILITY_PRIVATE
          enableVibration(item.getBoolean("vibration"))
          setShowBadge(item.getBoolean("badge"))
          val sound = item.getString("sound")
          setSound(when {
            sound == "none" -> null
            sound == "default" -> RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION)
            else -> Uri.parse("android.resource://${context.packageName}/${resource(context, sound.removePrefix("raw/"), "raw")}")
          }, AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_NOTIFICATION).build())
        })
      }
      val channel = manager.getNotificationChannel(id)
      result.put(JSONObject().put("id", id).put("importance", channel.importance)
        .put("sound", channel.sound != null).put("vibration", channel.shouldVibrate())
        .put("blocked", channel.importance == NotificationManager.IMPORTANCE_NONE))
    }
    return result
  }

  private fun validatePresentation (context: Context, options: JSONObject) {
    val group = text(options, "group", 256)
    val summary = flag(options, "groupSummary")
    val behavior = text(options, "groupAlertBehavior", 16, "all")
    require(behavior in listOf("all", "summary", "children") && (!summary || group.isNotEmpty()))
    if (options.has("smallIcon")) resource(context, text(options, "smallIcon", 128), "drawable")
    if (options.has("publicVersion")) {
      val public = options.getJSONObject("publicVersion")
      require(public.keys().asSequence().all { it in listOf("title", "body") })
      require(text(public, "title", 160).isNotEmpty())
      text(public, "body", 512)
    }
  }

  private fun applyPresentation (context: Context, builder: NotificationCompat.Builder, channel: String, options: JSONObject) {
    val group = options.optString("group")
    if (group.isNotEmpty()) builder.setGroup(group)
    builder.setGroupSummary(options.optBoolean("groupSummary"))
    builder.setGroupAlertBehavior(when (options.optString("groupAlertBehavior", "all")) {
      "summary" -> NotificationCompat.GROUP_ALERT_SUMMARY
      "children" -> NotificationCompat.GROUP_ALERT_CHILDREN
      else -> NotificationCompat.GROUP_ALERT_ALL
    })
    val icon = if (options.has("smallIcon")) resource(context, options.getString("smallIcon"), "drawable")
      else if (context.applicationInfo.icon != 0) context.applicationInfo.icon else android.R.drawable.ic_dialog_info
    if (options.has("smallIcon")) builder.setSmallIcon(icon)
    options.optJSONObject("publicVersion")?.let { public ->
      builder.setPublicVersion(NotificationCompat.Builder(context, channel).setSmallIcon(icon)
        .setContentTitle(public.getString("title")).setContentText(public.optString("body"))
        .setVisibility(NotificationCompat.VISIBILITY_PUBLIC).build())
    }
  }

  /** May be called from a compiled receiver/executor using Application context; no WebView is started. */
  fun submit (context: Context, options: JSONObject): JSONObject = submitNative(context, options, "")

  /** Native-only opaque target correlation. The renderer request validator does not accept targetRef. */
  fun submitNative (context: Context, options: JSONObject, targetRef: String): JSONObject {
    require(targetRef.length <= 256 && targetRef.none { it.code < 32 })
    if (!App.getInstance().hasRuntimePermission("notifications") ||
        !NotificationManagerCompat.from(context).areNotificationsEnabled()) throw PlatformFailure("EACCES")
    validatePresentation(context, options)
    if (options.has("actions")) {
      val actions = options.getJSONArray("actions")
      require(actions.length() <= 3)
      val identities = mutableSetOf<String>()
      for (index in 0 until actions.length()) {
        val action = actions.getJSONObject(index)
        require(text(action, "action", 256).isNotEmpty() && text(action, "title", 256).isNotEmpty())
        require(identities.add(action.getString("action")) && action.getString("action") !in listOf("default", "dismiss"))
      }
    }
    val id = text(options, "id", 256)
    val title = text(options, "title", 4096)
    require(id.isNotEmpty() && title.isNotEmpty())
    val requestedChannel = text(options, "channel", 256, context.packageName)
    val channel = if (requestedChannel == "default") context.packageName else requestedChannel
    val manager = context.getSystemService(NotificationManager::class.java)
    val nativeChannel = manager.getNotificationChannel(channel) ?: throw PlatformFailure("ENOENT")
    if (nativeChannel.importance == NotificationManager.IMPORTANCE_NONE) throw PlatformFailure("EACCES")
    val posted = context.post(id, title, text(options, "body", 16384), text(options, "tag", 256), channel,
      text(options, "category", 128), flag(options, "silent"), text(options, "icon", 4096), text(options, "image", 4096),
      text(options, "vibratePattern", 4096), (options.optJSONArray("actions") ?: JSONArray()).toString(),
      text(options, "dataJSON", 65536), flag(options, "requireInteraction"), flag(options, "renotify"), options, targetRef)
    if (!posted) throw PlatformFailure("EIO")
    return JSONObject().put("id", id).put("submitted", true).put("presentationObserved", false)
  }

  /** Read-only OS metadata; does not create channels or request permission. */
  fun status(context: Context, channels: JSONArray): JSONObject {
    if (!App.getInstance().hasRuntimePermission("notifications")) throw PlatformFailure("EACCES")
    require(channels.length() <= 32)
    val manager = context.getSystemService(NotificationManager::class.java)
    val values = JSONArray()
    for (index in 0 until channels.length()) {
      val id = channels.getString(index);require(id.length in 1..128)
      val channel = manager.getNotificationChannel(if (id == "default") context.packageName else id)
      values.put(JSONObject().put("id", id).put("exists", channel != null).put("importance", channel?.importance ?: 0)
        .put("sound", channel?.sound != null).put("vibration", channel?.shouldVibrate() ?: false)
        .put("blocked", channel != null && channel.importance == NotificationManager.IMPORTANCE_NONE))
    }
    return JSONObject().put("enabled", NotificationManagerCompat.from(context).areNotificationsEnabled()).put("channels", values)
  }

  /** Native-only inspection of this package, including retained legacy OS entries. No titles/body are returned. */
  fun active(context: Context): JSONArray {
    if (!App.getInstance().hasRuntimePermission("notifications")) throw PlatformFailure("EACCES")
    val manager = context.getSystemService(NotificationManager::class.java)
    val active = manager.activeNotifications
    if (active.size > 256) throw PlatformFailure("E2BIG")
    return JSONArray(active.map { posted ->
      val notification = posted.notification
      val nativeTag = posted.tag ?: ""
      val prefix = nativeTag.substringBefore(':')
      val size = nativeTag.substringAfter(':', "").substringBefore(':').toIntOrNull()
      val payload = nativeTag.substringAfter(':', "").substringAfter(':', "")
      val owned = prefix == "oro" && size != null && size in 1..256 && payload.length > size && payload[size] == ':'
      val id = if (owned) payload.substring(0, size!!) else ""
      val tag = if (owned) payload.substring(size!! + 1) else nativeTag
      JSONObject().put("id", id).put("tag", tag).put("legacy", !owned).put("channel", notification.channelId ?: "")
        .put("group", notification.group ?: "").put("summary", notification.flags and android.app.Notification.FLAG_GROUP_SUMMARY != 0)
        .put("private", notification.visibility == android.app.Notification.VISIBILITY_PRIVATE)
        .put("openAction", notification.contentIntent != null)
        .put("targetRef", if (owned) NotificationReceipts.target(context, id, tag) else "")
    })
  }

  internal fun request (call: PlatformCall, action: String, options: JSONObject) {
    call.foreground()
    when (action) {
      "status" -> {
        require(options.keys().asSequence().all { it == "channels" })
        call.success(status(call.app, options.optJSONArray("channels") ?: JSONArray()))
      }
      "active" -> {
        require(options.length() == 0)
        val values = active(call.app)
        call.success(JSONArray((0 until values.length()).map { values.getJSONObject(it) }
          .filter { !it.getBoolean("legacy") }.map { value ->
            value.remove("targetRef")
            value.remove("legacy")
            value
          }))
      }
      "configureChannels" -> {
        require(options.keys().asSequence().all { it == "channels" })
        call.success(configureChannels(call.app, options.getJSONArray("channels")))
      }
      "submit" -> {
        require(options.keys().asSequence().all { it in listOf("id", "title", "body", "tag", "channel", "category", "silent",
          "icon", "image", "vibratePattern", "actions", "dataJSON", "requireInteraction", "renotify", "group", "groupSummary",
          "groupAlertBehavior", "smallIcon", "publicVersion") })
        call.success(submit(call.app, options))
      }
      "cancel" -> {
        require(options.keys().asSequence().all { it in listOf("id", "tag") })
        val id = text(options, "id", 256)
        require(id.isNotEmpty())
        if (!cancel(call.app, id, text(options, "tag", 256))) throw PlatformFailure("EIO")
        call.success()
      }
      else -> throw PlatformFailure("ENOSYS")
    }
  }

  internal fun tag (id: String, tag: String): String = "oro:${id.length}:$id:$tag"

  private fun Context.post (
    id: String,
    title: String,
    body: String,
    tag: String,
    channel: String,
    category: String,
    silent: Boolean,
    iconURL: String,
    imageURL: String,
    vibratePattern: String,
    actionsJSON: String,
    dataJSON: String,
    requireInteraction: Boolean,
    renotify: Boolean,
    presentation: JSONObject,
    targetRef: String = ""
  ): Boolean {
    try {
      val notificationManager = getSystemService(NotificationManager::class.java)
      if (!App.getInstance().hasRuntimePermission("notifications") || !NotificationManagerCompat.from(this).areNotificationsEnabled()) return false
      if (id.length !in 1..256 || channel.length !in 1..256 || actionsJSON.length > 16_384 || dataJSON.length > 65_536) return false
      if (title.length > 4096 || body.length > 16_384 || tag.length > 256 || category.length > 128 ||
          iconURL.length > 4096 || imageURL.length > 4096 || vibratePattern.length > 4096) return false
      if (notificationManager.getNotificationChannel(channel)?.importance == NotificationManager.IMPORTANCE_NONE) return false
      if (notificationManager.getNotificationChannel(channel) == null) return false
      val launchIntent = packageManager.getLaunchIntentForPackage(packageName) ?: return false
      val identifier = id.toLongOrNull()?.toInt() ?: id.hashCode()
      val vibration = mutableListOf<Long>()
      for (duration in vibratePattern.split(',').filter { it.isNotEmpty() }) {
        val value = duration.toLongOrNull() ?: return false
        if (value !in 0..10000 || vibration.size >= 64) return false
        vibration.add(value)
      }

      val actions = mutableListOf<Pair<String, String>>()
      if (actionsJSON.isNotEmpty()) {
        try {
          val array = JSONArray(actionsJSON)
          for (index in 0 until minOf(3, array.length())) {
            val entry = array.optJSONObject(index) ?: return false
            val actionId = entry.optString("action")
            val actionTitle = entry.optString("title")
            if (actionId.isEmpty() || actionTitle.isEmpty()) return false
            if (actionId.length > 256) return false
            actions.add(Pair(actionId, actionTitle))
          }
        } catch (_: JSONException) {
          return false
        }
      }

      val intentFlags = Intent.FLAG_ACTIVITY_SINGLE_TOP

      val contentIntent = (NotificationReceipts.createIntent(this, Intent(launchIntent), id, "default", tag, targetRef) ?: return false).apply {
        addCategory(Intent.CATEGORY_LAUNCHER)
        setAction("notification.response.default")
        putExtra("id", id)
        putExtra("action", "default")
        if (dataJSON.isNotEmpty()) {
          putExtra("data", dataJSON)
        }
        flags = intentFlags
      }

      val deleteIntent = (NotificationReceipts.createIntent(
        this, Intent(this, NotificationDismissReceiver::class.java), id, "dismiss", tag, targetRef
      ) ?: return false).apply {
        setAction("notification.response.dismiss")
        putExtra("id", id)
        putExtra("action", "dismiss")
        if (dataJSON.isNotEmpty()) {
          putExtra("data", dataJSON)
        }
        flags = intentFlags
      }

      val pendingContentIntent = PendingIntent.getActivity(
        this,
        identifier,
        contentIntent,
        (
          PendingIntent.FLAG_UPDATE_CURRENT or
          PendingIntent.FLAG_IMMUTABLE or
          PendingIntent.FLAG_ONE_SHOT
        )
      )

      val pendingDeleteIntent = PendingIntent.getBroadcast(
        this,
        identifier,
        deleteIntent,
        (
          PendingIntent.FLAG_UPDATE_CURRENT or
          PendingIntent.FLAG_IMMUTABLE or
          PendingIntent.FLAG_ONE_SHOT
        )
      )

      val builder = NotificationCompat.Builder(this, channel).apply {
        setPriority(NotificationCompat.PRIORITY_DEFAULT)
        setContentTitle(title)
        setContentIntent(pendingContentIntent)
        setDeleteIntent(pendingDeleteIntent)
        setAutoCancel(!requireInteraction)
        setOngoing(requireInteraction)
        setOnlyAlertOnce(!renotify)
        setSilent(silent)
        setVisibility(NotificationCompat.VISIBILITY_PRIVATE)
        if (!silent && vibration.size > 0) {
          setVibrate(vibration.toLongArray())
        }

        if (body.isNotEmpty()) {
          setContentText(body)
        }
      }

      if (iconURL.isNotEmpty()) {
        val icon = IconCompat.createWithContentUri(iconURL)
        builder.setSmallIcon(icon)
      } else {
        val applicationIcon = applicationInfo.icon
        builder.setSmallIcon(
          if (applicationIcon != 0) applicationIcon else android.R.drawable.ic_dialog_info
        )
      }

      if (imageURL.isNotEmpty()) {
        val icon = Icon.createWithContentUri(imageURL)
        builder.setLargeIcon(icon)
      }

      if (category.isNotEmpty()) {
        builder.setCategory(
          category
            .replace("msg", "message")
            .replace("-", "_")
        )
      }

      var actionRequestCode = identifier + 1
      for ((actionId, actionTitle) in actions) {
        val actionIntent = (NotificationReceipts.createIntent(this, Intent(launchIntent), id, actionId, tag, targetRef) ?: return false).apply {
          setAction("notification.response.action")
          putExtra("id", id)
          putExtra("action", actionId)
          if (dataJSON.isNotEmpty()) {
            putExtra("data", dataJSON)
          }
          flags = intentFlags
        }

        val pendingActionIntent = PendingIntent.getActivity(
          this,
          actionRequestCode++,
          actionIntent,
          (
            PendingIntent.FLAG_UPDATE_CURRENT or
            PendingIntent.FLAG_IMMUTABLE or
            PendingIntent.FLAG_ONE_SHOT
          )
        )

        builder.addAction(0, actionTitle, pendingActionIntent)
      }

      applyPresentation(this, builder, channel, presentation)
      val notification = builder.build()
      NotificationManagerCompat.from(this).notify(NativeNotifications.tag(id, tag), identifier, notification)
      return true
    } catch (_: RuntimeException) {
      return false
    }
  }

}
