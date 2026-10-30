package com.secretkeeper.app.core

import java.sql.Connection
import java.sql.DriverManager

/** SqlDb 的 JDBC 适配器：单元测试里用 xerial sqlite-jdbc 跑真 SQLite。 */
class JdbcSqlDb(connection: Connection) : SqlDb {
    companion object {
        /** 内存库；也可传 "jdbc:sqlite:/path/to/file.db" 落盘。 */
        fun openInMemory(): JdbcSqlDb =
            JdbcSqlDb(DriverManager.getConnection("jdbc:sqlite::memory:"))
    }

    private val conn = connection

    override fun exec(sql: String) {
        conn.createStatement().use { st -> st.execute(sql) }
    }

    override fun execute(sql: String, bind: List<Any?>): Int =
        conn.prepareStatement(sql).use { st ->
            bindBind(st, bind)
            st.executeUpdate()
        }

    override fun query(sql: String, bind: List<Any?>, row: (SqlRow) -> Unit) {
        conn.prepareStatement(sql).use { st ->
            bindBind(st, bind)
            st.executeQuery().use { rs ->
                while (rs.next()) {
                    row(object : SqlRow {
                        override fun string(index: Int): String? = rs.getString(index + 1)

                        override fun long(index: Int): Long = rs.getLong(index + 1)
                    })
                }
            }
        }
    }

    override fun close() {
        conn.close()
    }

    private fun bindBind(st: java.sql.PreparedStatement, bind: List<Any?>) {
        bind.forEachIndexed { i, v ->
            when (v) {
                null -> st.setObject(i + 1, null)
                is String -> st.setString(i + 1, v)
                is Long -> st.setLong(i + 1, v)
                is Int -> st.setLong(i + 1, v.toLong())
                is Boolean -> st.setInt(i + 1, if (v) 1 else 0)
                else -> st.setObject(i + 1, v)
            }
        }
    }
}
