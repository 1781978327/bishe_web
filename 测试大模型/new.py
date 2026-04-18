import requests
import base64

api_key = "sk-wSQl8bUrBMDJuAPZKqgTtj2H7IA7VDSjbcYQFH2RwLSrKcYb"
base_url = "https://api.866646.xyz"

# 本地图片路径
image_path = r"/home/orangepi/Desktop/web/测试大模型/R-C.jpg"

# 读取图片并转 base64
with open(image_path, "rb") as f:
    base64_image = base64.b64encode(f.read()).decode("utf-8")

headers = {
    "Authorization": f"Bearer {api_key}",
    "Content-Type": "application/json"
}

data = {
    "model": "doubao-1.5-vision-pro",
    "messages": [
        {
            "role": "user",
            "content": [
                {"type": "text", "text": "这图片里是谁"},
                {
                    "type": "image_url",
                    "image_url": {
                        "url": f"data:image/jpeg;base64,{base64_image}"
                    }
                }
            ]
        }
    ]
}

try:
    response = requests.post(f"{base_url}/v1/chat/completions", json=data, headers=headers, timeout=900)
    if response.status_code == 200:
        result = response.json()
        print("✅ 图片测试成功！")
        print("AI 描述：")
        print(result["choices"][0]["message"]["content"])
    else:
        print("❌ 调用失败")
        print("状态码：", response.status_code)
        print("返回：", response.text)
except Exception as e:
    print("请求出错：", e)