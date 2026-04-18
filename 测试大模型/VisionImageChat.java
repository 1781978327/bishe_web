import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.ProxySelector;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.Base64;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class VisionImageChat {
    private static final Path WORK_DIR = Paths.get("/home/orangepi/Desktop/web/测试大模型");
    private static final Path PYTHON_EXAMPLE = WORK_DIR.resolve("new.py");
    private static final String DEFAULT_MODEL = "qwen3-vl:235b-instruct";

    public static void main(String[] args) {
        try {
            String apiKey = firstNonBlank(
                System.getenv("VISION_API_KEY"),
                System.getProperty("vision.apiKey"),
                readQuotedAssignment(PYTHON_EXAMPLE, "api_key")
            );
            String baseUrl = firstNonBlank(
                System.getenv("VISION_BASE_URL"),
                System.getProperty("vision.baseUrl"),
                readQuotedAssignment(PYTHON_EXAMPLE, "base_url"),
                "https://api.866646.xyz"
            );
            String proxyUrl = firstNonBlank(
                System.getenv("HTTPS_PROXY"),
                System.getenv("HTTP_PROXY"),
                System.getProperty("https.proxy"),
                System.getProperty("http.proxy")
            );
            String imagePathText = firstNonBlank(
                System.getenv("VISION_IMAGE_PATH"),
                System.getProperty("vision.imagePath"),
                readQuotedAssignment(PYTHON_EXAMPLE, "image_path"),
                WORK_DIR.resolve("R-C.jpg").toString()
            );
            String prompt = firstNonBlank(
                System.getenv("VISION_PROMPT"),
                System.getProperty("vision.prompt"),
                "这图片里是谁"
            );
            String model = firstNonBlank(
                System.getenv("VISION_MODEL"),
                System.getProperty("vision.model"),
                DEFAULT_MODEL
            );

            if (isBlank(apiKey)) {
                System.out.println("未找到 API Key。");
                System.out.println("你可以这样运行：");
                System.out.println("  export VISION_API_KEY=你的密钥");
                System.out.println("  java VisionImageChat.java");
                return;
            }

            Path imagePath = Paths.get(imagePathText);
            if (!Files.exists(imagePath)) {
                System.out.println("图片不存在: " + imagePath);
                return;
            }

            byte[] imageBytes = Files.readAllBytes(imagePath);
            String base64Image = Base64.getEncoder().encodeToString(imageBytes);

            String payload = "{"
                + "\"model\":\"" + escapeJson(model) + "\","
                + "\"messages\":[{"
                + "\"role\":\"user\","
                + "\"content\":["
                + "{\"type\":\"text\",\"text\":\"" + escapeJson(prompt) + "\"},"
                + "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64," + base64Image + "\"}}"
                + "]"
                + "}]"
                + "}";

            HttpClient.Builder clientBuilder = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(30))
                .version(HttpClient.Version.HTTP_1_1);

            if (!isBlank(proxyUrl)) {
                ProxySelector proxySelector = buildProxySelector(proxyUrl);
                if (proxySelector != null) {
                    clientBuilder.proxy(proxySelector);
                }
            }

            HttpClient client = clientBuilder.build();

            HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(trimTrailingSlash(baseUrl) + "/v1/chat/completions"))
                .timeout(Duration.ofSeconds(900))
                .version(HttpClient.Version.HTTP_1_1)
                .header("Authorization", "Bearer " + apiKey)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(payload, StandardCharsets.UTF_8))
                .build();

            System.out.println("正在请求视觉模型...");
            System.out.println("Base URL: " + baseUrl);
            System.out.println("Model: " + model);
            System.out.println("Image: " + imagePath);
            System.out.println("Prompt: " + prompt);
            System.out.println("HTTP Version: HTTP/1.1");
            System.out.println("Proxy: " + (isBlank(proxyUrl) ? "(none)" : proxyUrl));

            HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));

            if (response.statusCode() == 200) {
                String answer = extractAssistantContent(response.body());
                System.out.println("✅ 图片测试成功！");
                System.out.println("AI 描述：");
                if (!isBlank(answer)) {
                    System.out.println(answer);
                } else {
                    System.out.println(response.body());
                }
            } else {
                System.out.println("❌ 调用失败");
                System.out.println("状态码: " + response.statusCode());
                System.out.println("返回: " + response.body());
            }
        } catch (Exception e) {
            System.out.println("请求出错: " + e.getMessage());
            e.printStackTrace(System.out);
        }
    }

    private static String readQuotedAssignment(Path path, String variableName) {
        if (path == null || !Files.exists(path)) {
            return null;
        }
        try {
            String text = Files.readString(path, StandardCharsets.UTF_8);
            Pattern pattern = Pattern.compile("(?m)^\\s*" + Pattern.quote(variableName) + "\\s*=\\s*(?:r)?\"([^\"]*)\"");
            Matcher matcher = pattern.matcher(text);
            if (matcher.find()) {
                return matcher.group(1);
            }
        } catch (IOException ignored) {
        }
        return null;
    }

    private static String extractAssistantContent(String responseBody) {
        if (isBlank(responseBody)) {
            return null;
        }
        Pattern pattern = Pattern.compile("\"message\"\\s*:\\s*\\{.*?\"content\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"", Pattern.DOTALL);
        Matcher matcher = pattern.matcher(responseBody);
        if (!matcher.find()) {
            return null;
        }
        return unescapeJson(matcher.group(1));
    }

    private static String unescapeJson(String value) {
        if (value == null) {
            return null;
        }
        return value
            .replace("\\\\", "\\")
            .replace("\\\"", "\"")
            .replace("\\n", "\n")
            .replace("\\r", "\r")
            .replace("\\t", "\t")
            .replace("\\/", "/");
    }

    private static String escapeJson(String value) {
        if (value == null) {
            return "";
        }
        return value
            .replace("\\", "\\\\")
            .replace("\"", "\\\"")
            .replace("\n", "\\n")
            .replace("\r", "\\r")
            .replace("\t", "\\t");
    }

    private static String trimTrailingSlash(String value) {
        if (value == null) {
            return "";
        }
        int end = value.length();
        while (end > 0 && value.charAt(end - 1) == '/') {
            end--;
        }
        return value.substring(0, end);
    }

    private static String firstNonBlank(String... values) {
        for (String value : values) {
            if (!isBlank(value)) {
                return value;
            }
        }
        return null;
    }

    private static ProxySelector buildProxySelector(String proxyUrl) {
        try {
            URI uri = URI.create(proxyUrl);
            String host = uri.getHost();
            int port = uri.getPort();
            if (isBlank(host) || port <= 0) {
                return null;
            }
            return ProxySelector.of(new InetSocketAddress(host, port));
        } catch (Exception ignored) {
            return null;
        }
    }

    private static boolean isBlank(String value) {
        return value == null || value.trim().isEmpty();
    }
}
