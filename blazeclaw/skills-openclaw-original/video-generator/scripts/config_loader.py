import json
import os
import logging

logger = logging.getLogger(__name__)

CONFIG_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'config.json')

class Config:
    """统一配置管理类"""
    
    def __init__(self, config_path=None):
        self.config_path = config_path or CONFIG_FILE
        self.config = {}
        self.load_config()
    
    def load_config(self):
        """加载配置文件"""
        if not os.path.exists(self.config_path):
            logger.warning(f"配置文件 {self.config_path} 不存在，使用默认配置")
            self.config = self._get_default_config()
            return
        
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config = json.load(f)
            logger.info(f"配置文件加载成功：{self.config_path}")
        except Exception as e:
            logger.error(f"配置文件加载失败：{e}")
            self.config = self._get_default_config()
    
    def _get_default_config(self):
        """获取默认配置"""
        return {
            "api_key": "",
            "models": {
                "video_generation": "happyhorse-1.0-t2v"
            },
            "default_params": {
                "video_duration": 3,
                "video_resolution": "1080P"
            },
            "paths": {
                "output_folder": "outputs"
            }
        }
    
    def get_api_key(self):
        """获取 API Key"""
        api_key = self.config.get("api_key", "")
        if not api_key:
            logger.warning("API Key 未配置，请检查配置文件")
        return api_key
    
    def get_model(self, model_type):
        """获取指定类型的模型名称"""
        models = self.config.get("models", {})
        return models.get(model_type, "")
    
    def get_param(self, param_name, default=None):
        """获取默认参数"""
        params = self.config.get("default_params", {})
        return params.get(param_name, default)
    
    def get_path(self, path_name):
        """获取路径配置"""
        paths = self.config.get("paths", {})
        return paths.get(path_name, "")
    
    def get_all(self):
        """获取完整配置"""
        return self.config.copy()

# 全局配置实例
_config_instance = None

def get_config(config_path=None):
    """获取全局配置实例（单例模式）"""
    global _config_instance
    if _config_instance is None:
        _config_instance = Config(config_path)
    return _config_instance

def reload_config(config_path=None):
    """重新加载配置"""
    global _config_instance
    _config_instance = Config(config_path)
    return _config_instance
