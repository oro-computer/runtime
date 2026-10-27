package android.database.sqlite

import android.database.DatabaseErrorHandler
import java.sql.Connection
import java.sql.DriverManager
import java.sql.ResultSet
import java.sql.PreparedStatement

class SQLiteDatabaseLockedException: RuntimeException()
class SQLiteDatabaseCorruptException: RuntimeException()
class SQLiteFullException: RuntimeException()

/** Real SQLite JDBC transactions; fault switches target only scope writes. */
object DatabaseFault {
  var beforeCommit = false
  var afterCommit = false
}
class Cursor(private val result: ResultSet, private val statement: PreparedStatement): AutoCloseable {
  fun moveToNext() = result.next()
  fun moveToFirst() = result.next()
  fun getString(index: Int): String = result.getString(index + 1)
  fun getLong(index: Int) = result.getLong(index + 1)
  fun getInt(index: Int) = result.getInt(index + 1)
  fun getBlob(index: Int): ByteArray = result.getBytes(index + 1)
  override fun close() { result.close(); statement.close() }
}
class SQLiteDatabase private constructor(private val connection: Connection): AutoCloseable {
  private var successful = false
  private var scopeWrite = false
  var version: Int
    get() = rawQuery("PRAGMA user_version", null).use { it.moveToFirst(); it.getInt(0) }
    set(value) { execSQL("PRAGMA user_version=$value") }
  companion object {
    const val CREATE_IF_NECESSARY = 1
    const val NO_LOCALIZED_COLLATORS = 2
    fun openDatabase(path: String, cursor: Any?, flags: Int, handler: DatabaseErrorHandler): SQLiteDatabase {
      Class.forName("org.sqlite.JDBC")
      return SQLiteDatabase(DriverManager.getConnection("jdbc:sqlite:$path"))
    }
  }
  fun beginTransaction() { execSQL("BEGIN EXCLUSIVE"); successful = false; scopeWrite = false }
  fun setTransactionSuccessful() { successful = true }
  fun endTransaction() {
    if (successful && scopeWrite && DatabaseFault.beforeCommit) {
      DatabaseFault.beforeCommit = false; execSQL("ROLLBACK"); throw IllegalStateException("public-before-commit")
    }
    execSQL(if (successful) "COMMIT" else "ROLLBACK")
    if (successful && scopeWrite && DatabaseFault.afterCommit) {
      DatabaseFault.afterCommit = false; throw IllegalStateException("public-after-commit")
    }
  }
  fun rawQuery(sql: String, args: Array<String>?): Cursor {
    val statement = connection.prepareStatement(sql)
    args?.forEachIndexed { index, value -> statement.setString(index + 1, value) }
    return Cursor(statement.executeQuery(), statement)
  }
  fun execSQL(sql: String, args: Array<out Any?> = emptyArray()) {
    if (sql.startsWith("INSERT OR REPLACE INTO scopes")) scopeWrite = true
    connection.prepareStatement(sql).use { statement ->
      args.forEachIndexed { index, value ->
        if (value is ByteArray) statement.setBytes(index + 1, value) else statement.setObject(index + 1, value)
      }
      statement.execute()
    }
  }
  override fun close() { connection.close() }
}
