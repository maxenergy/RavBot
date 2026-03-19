package com.ravbot.android.network

import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URI
import java.net.URL
import java.net.URLDecoder
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import java.util.Locale
import org.json.JSONArray
import org.json.JSONObject

class HostWebToolClient {
  fun webSearch(
      sessionId: String,
      query: String,
      count: Int,
      freshness: String?,
  ): String {
    require(sessionId.isNotBlank()) { "sessionId is required" }
    require(query.isNotBlank()) { "query is required" }

    val cappedCount = count.coerceIn(1, 10)
    val encodedQuery = URLEncoder.encode(query, StandardCharsets.UTF_8.name())
    val freshnessParam =
        when (freshness) {
          "day" -> "&df=d"
          "week" -> "&df=w"
          "month" -> "&df=m"
          "year" -> "&df=y"
          else -> ""
        }
    val url =
        URL("https://html.duckduckgo.com/html/?q=$encodedQuery$freshnessParam")
    val html =
        openConnection(url).let { connection ->
          try {
            readBody(connection)
          } finally {
            connection.disconnect()
          }
        }

    val linkRegex =
        Regex(
            """<a\s+[^>]*class="result__a"[^>]*href="([^"]*)"[^>]*>([\s\S]*?)</a>""",
            setOf(RegexOption.IGNORE_CASE),
        )
    val snippetRegex =
        Regex(
            """<a\s+[^>]*class="result__snippet"[^>]*>([\s\S]*?)</a>""",
            setOf(RegexOption.IGNORE_CASE),
        )

    val links =
        linkRegex.findAll(html).map { match ->
          val rawUrl = match.groupValues[1]
          val title = htmlToText(match.groupValues[2])
          decodeDuckDuckGoRedirect(rawUrl) to title
        }
    val snippets = snippetRegex.findAll(html).map { htmlToText(it.groupValues[1]) }.toList()

    val results = JSONArray()
    links.take(cappedCount).forEachIndexed { index, (urlValue, titleValue) ->
      results.put(
          JSONObject()
              .put("title", titleValue)
              .put("url", urlValue)
              .put("description", snippets.getOrElse(index) { "" }),
      )
    }

    return JSONObject()
        .put("provider", "android_host_duckduckgo")
        .put("sessionKey", sessionId)
        .put("query", query)
        .put("freshness", freshness ?: "")
        .put("results", results)
        .toString()
  }

  fun webFetch(
      sessionId: String,
      url: String,
      maxChars: Int,
  ): String {
    require(sessionId.isNotBlank()) { "sessionId is required" }
    require(url.isNotBlank()) { "url is required" }
    val cappedMaxChars = maxChars.coerceIn(256, 100_000)
    val parsed = URI(url)
    val scheme = parsed.scheme?.lowercase(Locale.US) ?: ""
    require(scheme == "http" || scheme == "https") {
      "Only http:// and https:// URLs are supported"
    }
    val host = parsed.host?.lowercase(Locale.US) ?: throw IllegalArgumentException("URL host is required")
    require(!isBlockedHost(host)) { "SSRF guard: blocked host $host" }

    val connection = openConnection(URL(url))
    val body: String
    val contentType: String
    try {
      body = readBody(connection)
      contentType = connection.contentType ?: "application/octet-stream"
    } finally {
      connection.disconnect()
    }
    val text =
        truncateContent(
            when {
              contentType.contains("html", ignoreCase = true) -> htmlToText(body)
              contentType.contains("json", ignoreCase = true) -> prettyJsonOrRaw(body)
              else -> body
            },
            cappedMaxChars,
        )

    return JSONObject()
        .put("sessionKey", sessionId)
        .put("url", url)
        .put("content", text)
        .put("contentType", contentType)
        .toString()
  }

  private fun openConnection(url: URL): HttpURLConnection {
    val connection = (url.openConnection() as HttpURLConnection)
    connection.instanceFollowRedirects = true
    connection.connectTimeout = 10_000
    connection.readTimeout = 20_000
    connection.setRequestProperty(
        "User-Agent",
        "Mozilla/5.0 (Android 12; Mobile) AppleWebKit/537.36 "
            + "(KHTML, like Gecko) Chrome/122.0.0.0 Mobile Safari/537.36",
    )
    connection.setRequestProperty(
        "Accept",
        "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    )
    connection.setRequestProperty("Accept-Language", "en-US,en;q=0.9")
    connection.requestMethod = "GET"
    val status = connection.responseCode
    if (status >= 400) {
      val error = connection.errorStream?.bufferedReader()?.use { it.readText() } ?: ""
      connection.disconnect()
      throw IllegalStateException("HTTP $status ${error.take(240)}".trim())
    }
    return connection
  }

  private fun readBody(connection: HttpURLConnection): String =
      BufferedReader(InputStreamReader(connection.inputStream)).use { reader ->
        reader.readText()
      }

  private fun htmlToText(html: String): String {
    var text = html.replace(Regex("<(script|style)[^>]*>[\\s\\S]*?</(script|style)>", RegexOption.IGNORE_CASE), " ")
    text = text.replace(Regex("<[^>]+>"), " ")
    text = decodeBasicEntities(text)
    text = text.replace(Regex("[ \\t]+"), " ")
    text = text.replace(Regex("\\n{3,}"), "\n\n")
    return text.trim()
  }

  private fun decodeBasicEntities(input: String): String =
      input
          .replace("&amp;", "&")
          .replace("&lt;", "<")
          .replace("&gt;", ">")
          .replace("&quot;", "\"")
          .replace("&#39;", "'")
          .replace("&nbsp;", " ")

  private fun decodeDuckDuckGoRedirect(rawUrl: String): String {
    val marker = "uddg="
    val markerIndex = rawUrl.indexOf(marker)
    if (markerIndex < 0) {
      return rawUrl
    }
    val start = markerIndex + marker.length
    val end = rawUrl.indexOf('&', start).takeIf { it >= 0 } ?: rawUrl.length
    return URLDecoder.decode(rawUrl.substring(start, end), StandardCharsets.UTF_8.name())
  }

  private fun prettyJsonOrRaw(body: String): String =
      runCatching { JSONObject(body).toString(2) }
          .recoverCatching { JSONArray(body).toString(2) }
          .getOrElse { body }

  private fun truncateContent(content: String, maxChars: Int): String {
    if (content.length <= maxChars) {
      return content
    }
    return content.take(maxChars) + "\n\n[truncated at $maxChars chars]"
  }

  private fun isBlockedHost(host: String): Boolean {
    if (host == "localhost" || host == "::1") {
      return true
    }
    if (host.startsWith("127.") ||
        host.startsWith("10.") ||
        host.startsWith("192.168.") ||
        host.startsWith("169.254.") ||
        host == "0.0.0.0") {
      return true
    }
    if (host.startsWith("172.")) {
      val secondOctet = host.split('.').getOrNull(1)?.toIntOrNull()
      if (secondOctet != null && secondOctet in 16..31) {
        return true
      }
    }
    return false
  }
}
