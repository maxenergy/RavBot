package com.ravbot.android.network

import org.junit.Assert.assertTrue
import org.junit.Test

class HostWebToolClientTest {
  private val client = HostWebToolClient()

  @Test
  fun webFetchRejectsBlockedHost() {
    val error =
        runCatching { client.webFetch("http://127.0.0.1/secret", 4096) }.exceptionOrNull()

    requireNotNull(error)
    assertTrue(error is IllegalArgumentException)
    assertTrue(error.message.orEmpty().contains("SSRF guard"))
  }

  @Test
  fun webFetchRejectsUnsupportedScheme() {
    val error =
        runCatching { client.webFetch("ftp://example.com/file", 4096) }.exceptionOrNull()

    requireNotNull(error)
    assertTrue(error is IllegalArgumentException)
    assertTrue(error.message.orEmpty().contains("Only http:// and https://"))
  }

  @Test
  fun webSearchRejectsBlankQuery() {
    val error = runCatching { client.webSearch("", 5, null) }.exceptionOrNull()

    requireNotNull(error)
    assertTrue(error is IllegalArgumentException)
    assertTrue(error.message.orEmpty().contains("query is required"))
  }
}
