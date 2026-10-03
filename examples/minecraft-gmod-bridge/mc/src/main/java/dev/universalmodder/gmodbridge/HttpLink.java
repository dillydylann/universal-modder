package dev.universalmodder.gmodbridge;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.Executors;

/**
 * The host's connection: an HTTP server on 127.0.0.1 (port 25600, or -Dgmodbridge.port). Garry's Mod Lua has HTTP
 * but no sockets, so the host POSTs its state to /tick every tick and the reply carries everything that happened here
 * since its last poll. GMod only allows requests to localhost when started with -allowlocalhttp.
 *
 * <p>Every request must carry the header {@code X-MCBridge: 1} and no {@code Origin}: a web page in a browser can't
 * send a custom header to another origin without a CORS preflight, which this server never answers, so a page can't
 * drive Minecraft through it.
 */
final class HttpLink {
	static final String HEADER = "X-MCBridge";
	private static final int MAX_BODY = 8 << 20;

	private HttpLink() {
	}

	static void launch() {
		int port = Integer.getInteger("gmodbridge.port", 25600);
		try {
			HttpServer http = HttpServer.create(new InetSocketAddress(InetAddress.getLoopbackAddress(), port), 16);
			http.setExecutor(Executors.newSingleThreadExecutor(r -> {
				Thread t = new Thread(r, "gmodbridge-http");
				t.setDaemon(true);
				return t;
			}));
			http.createContext("/tick", HttpLink::tick);
			http.createContext("/hello", exchange -> reply(exchange, 200, Bridge.hello()));
			http.start();
			GmodBridge.LOG.info("gmod link listening on http://127.0.0.1:{}/tick", port);
		} catch (IOException e) {
			GmodBridge.LOG.error("gmod link: can't listen on 127.0.0.1:{}", port, e);
		}
	}

	private static void tick(final HttpExchange exchange) throws IOException {
		try {
			if (!"POST".equals(exchange.getRequestMethod())) {
				reply(exchange, 405, "{\"error\":\"POST only\"}");
				return;
			}

			if (!"1".equals(exchange.getRequestHeaders().getFirst(HEADER)) || exchange.getRequestHeaders().containsKey("Origin")) {
				reply(exchange, 403, "{\"error\":\"forbidden\"}");
				return;
			}

			byte[] body;
			try (InputStream in = exchange.getRequestBody()) {
				body = in.readNBytes(MAX_BODY + 1);
			}

			if (body.length > MAX_BODY) {
				reply(exchange, 413, "{\"error\":\"too large\"}");
				return;
			}

			JsonObject message;
			try {
				message = JsonParser.parseString(new String(body, StandardCharsets.UTF_8)).getAsJsonObject();
			} catch (RuntimeException e) {
				reply(exchange, 400, "{\"error\":\"bad json\"}");
				return;
			}

			Bridge.fromHost(message);
			reply(exchange, 200, Bridge.forHost());
		} catch (RuntimeException e) {
			GmodBridge.LOG.warn("gmod link: request failed", e);
			reply(exchange, 500, "{\"error\":\"internal\"}");
		}
	}

	private static void reply(final HttpExchange exchange, final int status, final String json) throws IOException {
		byte[] out = json.getBytes(StandardCharsets.UTF_8);
		exchange.getResponseHeaders().set("Content-Type", "application/json");
		exchange.sendResponseHeaders(status, out.length);
		try (OutputStream o = exchange.getResponseBody()) {
			o.write(out);
		}
	}
}
