# Baidu API Key Setup Guide (BlazeClaw)

## BAIDU_API_KEY Not Configured

When the `BAIDU_API_KEY` environment variable is not set, follow these steps:

### 1. Get API Key

Visit: **https://console.bce.baidu.com/ai-search/qianfan/ais/console/apiKey**

- Log in to your Baidu Cloud account
- Create an application or view existing API keys
- Copy your API Key

### 2. Configure environment

PowerShell (current session):

```powershell
$env:BAIDU_API_KEY = "your_actual_api_key_here"
```

Persist for new shells:

```powershell
setx BAIDU_API_KEY "your_actual_api_key_here"
```

### 3. Restart BlazeClaw process

Restart Visual Studio / BlazeClaw host process so it can read updated env vars.

### 4. Test

```powershell
python blazeclaw/skills/baidu-search/scripts/search.py '{"query":"test search"}'
```

## Troubleshooting

- Confirm key is valid and service is enabled in Baidu Cloud
- Ensure the running process inherited `BAIDU_API_KEY`
- Check outbound network access to `qianfan.baidubce.com`
