/**
 * 智能花盆 AI - 随机森林决策规则 (STM32 部署)
 * 模型: RandomForest(n_estimators=10, max_depth=5)
 * 训练准确率: 0.9667  测试准确率: 1.0000
 * 输入: 温度(°C) 空气湿度(%) 土壤湿度(%) 光照强度(lux)
 * 输出: 0=健康生长  1=光照不足  2=温度异常  3=缺水预警  (问题7修复)
 */
#include <stdint.h>

/* 问题7修复：enum顺序匹配Python训练模型的实际标签 */
typedef enum {
    STATUS_HEALTHY = 0,     // 0: 健康生长
    STATUS_LIGHT_ALERT,     // 1: 光照不足
    STATUS_TEMP_ALERT,      // 2: 温度异常
    STATUS_WATER_ALERT      // 3: 缺水预警
} PlantStatus;

static uint8_t tree_0_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (light_intensity <= 750.55f) {
    if (temperature <= 19.79f) {
            return 2;  // 温度异常
    } else {
            return 1;  // 光照不足
    }} else {
    if (temperature <= 37.08f) {
        if (temperature <= 15.29f) {
                return 2;  // 温度异常
        } else {
            if (soil_moisture <= 35.91f) {
                if (soil_moisture <= 21.14f) {
                        return 3;  // 缺水预警
                } else {
                        return 2;  // 温度异常
                }            } else {
                if (light_intensity <= 1535.24f) {
                        return 0;  // 健康生长
                } else {
                        return 0;  // 健康生长
                }            }        }    } else {
            return 2;  // 温度异常
    }}}
static uint8_t tree_1_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (light_intensity <= 747.82f) {
    if (soil_moisture <= 52.62f) {
            return 3;  // 缺水预警
    } else {
        if (soil_moisture <= 61.19f) {
                return 1;  // 光照不足
        } else {
                return 1;  // 光照不足
        }    }} else {
    if (soil_moisture <= 41.41f) {
        if (temperature <= 37.58f) {
            if (temperature <= 14.56f) {
                    return 2;  // 温度异常
            } else {
                if (soil_moisture <= 28.38f) {
                        return 3;  // 缺水预警
                } else {
                        return 2;  // 温度异常
                }            }        } else {
                return 2;  // 温度异常
        }    } else {
        if (temperature <= 15.20f) {
                return 2;  // 温度异常
        } else {
            if (air_humidity <= 38.06f) {
                if (air_humidity <= 28.64f) {
                        return 0;  // 健康生长
                } else {
                        return 2;  // 温度异常
                }            } else {
                if (temperature <= 36.80f) {
                        return 0;  // 健康生长
                } else {
                        return 2;  // 温度异常
                }            }        }    }}}
static uint8_t tree_2_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (temperature <= 35.15f) {
    if (light_intensity <= 1016.71f) {
        if (air_humidity <= 57.81f) {
            if (soil_moisture <= 57.69f) {
                    return 3;  // 缺水预警
            } else {
                    return 1;  // 光照不足
            }        } else {
                return 1;  // 光照不足
        }    } else {
        if (temperature <= 15.20f) {
                return 2;  // 温度异常
        } else {
            if (air_humidity <= 41.97f) {
                if (soil_moisture <= 46.33f) {
                        return 3;  // 缺水预警
                } else {
                        return 0;  // 健康生长
                }            } else {
                if (air_humidity <= 64.90f) {
                        return 0;  // 健康生长
                } else {
                        return 0;  // 健康生长
                }            }        }    }} else {
    if (air_humidity <= 69.03f) {
            return 2;  // 温度异常
    } else {
            return 2;  // 温度异常
    }}}
static uint8_t tree_3_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (soil_moisture <= 38.28f) {
    if (temperature <= 36.71f) {
        if (soil_moisture <= 27.93f) {
            if (soil_moisture <= 13.36f) {
                    return 2;  // 温度异常
            } else {
                if (temperature <= 24.75f) {
                        return 3;  // 缺水预警
                } else {
                        return 3;  // 缺水预警
                }            }        } else {
                return 2;  // 温度异常
        }    } else {
            return 2;  // 温度异常
    }} else {
    if (temperature <= 35.29f) {
        if (light_intensity <= 1016.71f) {
            if (temperature <= 18.41f) {
                    return 2;  // 温度异常
            } else {
                    return 1;  // 光照不足
            }        } else {
            if (air_humidity <= 29.52f) {
                    return 0;  // 健康生长
            } else {
                if (temperature <= 15.20f) {
                        return 2;  // 温度异常
                } else {
                        return 0;  // 健康生长
                }            }        }    } else {
            return 2;  // 温度异常
    }}}
static uint8_t tree_4_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (light_intensity <= 751.43f) {
    if (soil_moisture <= 42.80f) {
            return 3;  // 缺水预警
    } else {
            return 1;  // 光照不足
    }} else {
    if (soil_moisture <= 38.28f) {
        if (air_humidity <= 68.88f) {
            if (temperature <= 15.54f) {
                    return 2;  // 温度异常
            } else {
                if (temperature <= 33.91f) {
                        return 3;  // 缺水预警
                } else {
                        return 2;  // 温度异常
                }            }        } else {
                return 2;  // 温度异常
        }    } else {
        if (air_humidity <= 91.04f) {
            if (temperature <= 15.12f) {
                    return 2;  // 温度异常
            } else {
                if (air_humidity <= 75.00f) {
                        return 0;  // 健康生长
                } else {
                        return 2;  // 温度异常
                }            }        } else {
                return 0;  // 健康生长
        }    }}}
static uint8_t tree_5_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (light_intensity <= 1016.71f) {
    if (soil_moisture <= 16.73f) {
            return 3;  // 缺水预警
    } else {
        if (temperature <= 36.53f) {
            if (temperature <= 21.69f) {
                    return 1;  // 光照不足
            } else {
                    return 1;  // 光照不足
            }        } else {
                return 2;  // 温度异常
        }    }} else {
    if (light_intensity <= 2288.17f) {
        if (light_intensity <= 1951.62f) {
            if (soil_moisture <= 41.41f) {
                    return 2;  // 温度异常
            } else {
                if (temperature <= 20.98f) {
                        return 0;  // 健康生长
                } else {
                        return 0;  // 健康生长
                }            }        } else {
                return 3;  // 缺水预警
        }    } else {
        if (temperature <= 35.24f) {
            if (temperature <= 15.17f) {
                    return 2;  // 温度异常
            } else {
                if (soil_moisture <= 31.17f) {
                        return 3;  // 缺水预警
                } else {
                        return 0;  // 健康生长
                }            }        } else {
            if (soil_moisture <= 18.76f) {
                    return 2;  // 温度异常
            } else {
                    return 2;  // 温度异常
            }        }    }}}
static uint8_t tree_6_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (temperature <= 35.25f) {
    if (temperature <= 15.20f) {
            return 2;  // 温度异常
    } else {
        if (light_intensity <= 1067.37f) {
            if (soil_moisture <= 47.14f) {
                    return 3;  // 缺水预警
            } else {
                    return 1;  // 光照不足
            }        } else {
            if (light_intensity <= 3888.03f) {
                if (air_humidity <= 29.55f) {
                        return 3;  // 缺水预警
                } else {
                        return 0;  // 健康生长
                }            } else {
                if (soil_moisture <= 34.67f) {
                        return 3;  // 缺水预警
                } else {
                        return 0;  // 健康生长
                }            }        }    }} else {
    if (temperature <= 37.08f) {
            return 2;  // 温度异常
    } else {
            return 2;  // 温度异常
    }}}
static uint8_t tree_7_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (temperature <= 35.25f) {
    if (light_intensity <= 1016.71f) {
        if (air_humidity <= 57.81f) {
                return 1;  // 光照不足
        } else {
            if (soil_moisture <= 52.83f) {
                    return 1;  // 光照不足
            } else {
                    return 1;  // 光照不足
            }        }    } else {
        if (soil_moisture <= 31.76f) {
            if (light_intensity <= 3051.99f) {
                    return 3;  // 缺水预警
            } else {
                if (air_humidity <= 39.01f) {
                        return 3;  // 缺水预警
                } else {
                        return 3;  // 缺水预警
                }            }        } else {
            if (temperature <= 15.43f) {
                    return 2;  // 温度异常
            } else {
                    return 0;  // 健康生长
            }        }    }} else {
    if (light_intensity <= 774.24f) {
            return 2;  // 温度异常
    } else {
            return 2;  // 温度异常
    }}}
static uint8_t tree_8_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (temperature <= 35.16f) {
    if (soil_moisture <= 27.11f) {
        if (light_intensity <= 2185.16f) {
                return 3;  // 缺水预警
        } else {
                return 2;  // 温度异常
        }    } else {
        if (air_humidity <= 65.52f) {
            if (light_intensity <= 1110.54f) {
                    return 1;  // 光照不足
            } else {
                if (light_intensity <= 4647.58f) {
                        return 0;  // 健康生长
                } else {
                        return 0;  // 健康生长
                }            }        } else {
            if (temperature <= 14.82f) {
                    return 2;  // 温度异常
            } else {
                if (light_intensity <= 950.10f) {
                        return 1;  // 光照不足
                } else {
                        return 0;  // 健康生长
                }            }        }    }} else {
    if (temperature <= 37.08f) {
            return 2;  // 温度异常
    } else {
            return 2;  // 温度异常
    }}}
static uint8_t tree_9_predict(float temperature, float air_humidity, float soil_moisture, float light_intensity) {
if (light_intensity <= 762.45f) {
    if (light_intensity <= 246.42f) {
            return 2;  // 温度异常
    } else {
        if (soil_moisture <= 47.74f) {
                return 1;  // 光照不足
        } else {
                return 1;  // 光照不足
        }    }} else {
    if (temperature <= 34.73f) {
        if (soil_moisture <= 30.08f) {
            if (air_humidity <= 56.05f) {
                if (soil_moisture <= 17.82f) {
                        return 2;  // 温度异常
                } else {
                        return 3;  // 缺水预警
                }            } else {
                    return 3;  // 缺水预警
            }        } else {
            if (temperature <= 15.29f) {
                    return 2;  // 温度异常
            } else {
                if (air_humidity <= 36.50f) {
                        return 0;  // 健康生长
                } else {
                        return 0;  // 健康生长
                }            }        }    } else {
        if (temperature <= 37.08f) {
                return 2;  // 温度异常
        } else {
                return 2;  // 温度异常
        }    }}}

PlantStatus random_forest_predict(float temperature, float air_humidity,
                                  float soil_moisture, float light_intensity) {
    int votes[4] = {0};
    votes[tree_0_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_1_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_2_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_3_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_4_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_5_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_6_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_7_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_8_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;
    votes[tree_9_predict(temperature, air_humidity, soil_moisture, light_intensity)]++;

    uint8_t best = 0;
    for (int i = 1; i < 4; i++)
        if (votes[i] > votes[best]) best = i;
    return (PlantStatus)best;
}

/* 问题7修复：字符串映射匹配修正后的enum */
const char* status_to_string(PlantStatus s) {
    switch (s) {
        case STATUS_HEALTHY:     return "健康生长";
        case STATUS_LIGHT_ALERT: return "光照不足";
        case STATUS_TEMP_ALERT:  return "温度异常";
        case STATUS_WATER_ALERT: return "缺水预警";
        default:                 return "未知";
    }
}
