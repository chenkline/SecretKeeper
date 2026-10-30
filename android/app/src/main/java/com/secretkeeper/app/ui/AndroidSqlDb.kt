package com.secretkeeper.app.ui

import android.database.sqlite.SQLiteDatabase
import com.secretkeeper.app.core.SqlDb
import com.secretkeeper.app.core.SqlRow

/**
 * SqlDb 的 Android framework SQLite 适配器（运行时用）。
 * SQL 与桌面规范 §6.3 的标准子集一致；单元测试走 xerial JDBC 适配器。
 */
class AndroidSqlDb private constructor(private val db: SQLiteDatabase) : SqlDb {

    companion object {
        /** 打开（或创建）数据库文件。 */
        fun open(path: String): AndroidSqlDb =
            AndroidSqlDb(SQLiteDatabase.openOrCreateDatabase(path, null))
    }

    override fun exec(sql: String) {
        // CREATE TABLE / INDEX 等语句逐条执行（与 IndexDb.createSchema 拆分约定一致）
        db.execSQL(sql)
    }

    override fun execute(sql: String, bind: List<Any?>): Int {
        val stmt = db.compileStatement(sql)
        bind.forEachIndexed { i, v -> bindTo(stmt, i + 1, v) }
        return stmt.executeUpdateDelete()
    }

    override fun query(sql: String, bind: List<Any?>, row: (SqlRow) -> Unit) {
        // rawQuery 只接受字符串参数；本层查询绑定全部是 TEXT 列（ID/key），
        // 数字列只在 executeUpdateDelete（compileStatement 支持 Long 绑定）中使用。
        val args = bind.map { toSqlValue(it)?.toString() }.toTypedArray()
        db.rawQuery(sql, args).use { cursor ->
            while (cursor.moveToNext()) {
                row(object : SqlRow {
                    override fun string(index: Int): String? =
                        if (cursor.isNull(index)) null else cursor.getString(index)

                    override fun long(index: Int): Long = cursor.getLong(index)
                })
            }
        }
    }

    override fun close() {
        db.close()
    }

    private fun toSqlValue(v: Any?): Any? = when (v) {
        is Boolean -> if (v) 1L else 0L
        is Int -> v.toLong()
        else -> v
    }

    private fun bindTo(stmt: android.database.sqlite.SQLiteStatement, index: Int, v: Any?) {
        when (v) {
            null -> stmt.bindNull(index)
            is String -> stmt.bindString(index, v)
            is Long -> stmt.bindLong(index, v)
            is Int -> stmt.bindLong(index, v.toLong())
            is Boolean -> stmt.bindLong(index, if (v) 1L else 0L)
            is ByteArray -> stmt.bindBlob(index, v)
            else -> stmt.bindString(index, v.toString())
        }
    }
}
