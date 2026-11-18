package com.voyemu.prepare;

import java.io.InputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.time.Duration;
import java.util.Base64;
import java.util.HexFormat;
import java.util.Map;
import java.util.UUID;

final class Downloads {
    static Path cached(Map<String, Object> pin, Path cache, long limit, boolean base64)
            throws Exception {
        String expected = Json.str(pin, "sha256").toLowerCase(java.util.Locale.ROOT);

        Failure.require(
                expected.matches("[a-f0-9]{64}"),
                "DOWNLOAD_PIN_INVALID",
                "Download requires SHA256",
                3);
        URI source;

        try {
            source = URI.create(Json.str(pin, "url"));
        } catch (IllegalArgumentException e) {
            throw new Failure("DOWNLOAD_URL_INVALID", "Invalid pinned URL", 3);
        }

        validUrl(source);
        Path target = Storage.confined(cache, expected);

        if (Files.exists(target)) {
            Failure.require(
                    FilesSupport.hash(target).equals(expected),
                    "DOWNLOAD_HASH_MISMATCH",
                    "Cached input differs from its pin",
                    3);

            return target;
        }

        Storage.directory(cache);
        Path temporary = cache.resolve("." + expected + "." + UUID.randomUUID() + ".partial");
        HttpClient client =
                HttpClient.newBuilder()
                        .connectTimeout(Duration.ofSeconds(30))
                        .followRedirects(HttpClient.Redirect.NEVER)
                        .build();

        try {
            URI current = source;
            HttpResponse<InputStream> response = null;

            for (int redirects = 0; redirects <= 4; redirects++) {
                HttpRequest request =
                        HttpRequest.newBuilder(current)
                                .timeout(Duration.ofMinutes(15))
                                .header("User-Agent", "voyemu/1.0.0")
                                .GET()
                                .build();
                response = client.send(request, HttpResponse.BodyHandlers.ofInputStream());

                if (response.statusCode() == 200) {
                    break;
                }

                if (response.statusCode() >= 300 && response.statusCode() <= 399) {
                    String location = response.headers().firstValue("location").orElse("");
                    response.body().close();
                    Failure.require(
                            !location.isEmpty() && redirects < 4,
                            "DOWNLOAD_REDIRECT_INVALID",
                            "Invalid redirect",
                            3);
                    current = current.resolve(location);
                    validUrl(current);
                    continue;
                }

                response.body().close();
                throw new Failure(
                        "DOWNLOAD_HTTP_FAILED",
                        "Pinned download returned HTTP " + response.statusCode(),
                        3);
            }

            Failure.require(
                    response != null && response.statusCode() == 200,
                    "DOWNLOAD_HTTP_FAILED",
                    "No successful response",
                    3);
            MessageDigest hash = MessageDigest.getInstance("SHA-256");
            long size = 0;

            try (InputStream input =
                            base64
                                    ? Base64.getMimeDecoder().wrap(response.body())
                                    : response.body();
                    var output =
                            Files.newOutputStream(
                                    temporary, java.nio.file.StandardOpenOption.CREATE_NEW)) {
                byte[] buffer = new byte[128 * 1024];

                for (int n; (n = input.read(buffer)) >= 0; ) {
                    if (Thread.currentThread().isInterrupted()) {
                        throw new Failure("ABORTED", "Operation interrupted", 130);
                    }

                    if (n == 0) {
                        continue;
                    }

                    size += n;
                    Failure.require(
                            size <= limit, "DOWNLOAD_SIZE_LIMIT", "Download exceeds size limit", 3);
                    hash.update(buffer, 0, n);
                    output.write(buffer, 0, n);
                }
            }

            Failure.require(
                    HexFormat.of().formatHex(hash.digest()).equals(expected),
                    "DOWNLOAD_HASH_MISMATCH",
                    "Downloaded input differs from its SHA256 pin",
                    3);

            if (pin.containsKey("bytes")) {
                Failure.require(
                        size == Json.integer(pin, "bytes"),
                        "DOWNLOAD_SIZE_MISMATCH",
                        "Pinned byte count differs",
                        3);
            }

            Files.move(temporary, target, StandardCopyOption.ATOMIC_MOVE);

            return target;
        } finally {
            Files.deleteIfExists(temporary);
        }
    }

    private static void validUrl(URI uri) {
        Failure.require(
                "https".equals(uri.getScheme())
                        && uri.getHost() != null
                        && uri.getUserInfo() == null
                        && uri.getFragment() == null,
                "DOWNLOAD_URL_INVALID",
                "Pinned downloads require an HTTPS URL without credentials",
                3);
    }
}
