package android.database

fun interface DatabaseErrorHandler {
  fun onCorruption(database: android.database.sqlite.SQLiteDatabase)
}
