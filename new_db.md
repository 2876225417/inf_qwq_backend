
# Create New Database

Create the corresponding database in need.

DB: PostgreSQL 

## RTSP Stream Info

| 字段               | 类型                   | 描述                                                 |
| ----------------   | ---------------------- | ---------------------------------------------------- |
| rtsp_id            | SERIAL PRIMARY KEY     | RTSP 流唯一ID标识                                    |
| rtsp_type          |                        | RTSP 类型(海康，大华)                                |
| rtsp_username      | VARCHAR(50) NOT NULL   | RTSP 配置连接用户名                                  |
| rtsp_ip            | VARCHAR(50) NOT NULL   | RTSP 配置连接ip                                      |
| rtsp_port          | INTEGER NOT NULL       | RTSP 配置连接端口                                    |
| rtsp_channel       | VARCHAR(50) NOT NULL   | RTSP 配置连接 channel                                | 
| rtsp_subtype       | VARCHAR(50)            | RTSP 配置连接 subtype                                |
| rtsp_url           | VARCHAR(255) NOT NULL  | RTSP 配置连接 url                                    |
| rtsp_name          | VARCHAR(50) NOT NULL   | 自定义 RTSP 流显示名称                               |
| rtsp_crop_coord_x  |                        | 裁剪 RTSP 流的坐标位置(x, y, dx, dy)百分比 x(float)  |
| rtsp_crop_coord_dx |                        | 裁剪 RTSP 流的坐标位置(x, y, dx, dy)百分比 y(float)  |                
| rtsp_crop_coord_y  |                        | 裁剪 RTSP 流的坐标位置(x, y, dx, dy)百分比 dx(float) |
| rtsp_crop_coord_dy |                        | 裁剪 RTSP 流的坐标位置(x, y, dx, dy)百分比 dy(float) |

```sql
CREATE TABLE rtsp_stream_info (
    rtsp_id SERIAL PRIMARY KEY,
    rtsp_type VARCHAR(50),
    rtsp_username VARCHAR(50) NOT NULL,
    rtsp_ip VARCHAR(50) NOT NULL,
    rtsp_port INTEGER NOT NULL,
    rtsp_channel VARCHAR(50) NOT NULL,
    rtsp_subtype VARCHAR(50),
    rtsp_url VARCHAR(255) NOT NULL,
    rtsp_name VARCHAR(50) NOT NULL,
    rtsp_crop_coord_x FLOAT,
    rtsp_crop_coord_y FLOAT,
    rtsp_crop_coord_dx FLOAT,
    rtsp_crop_coord_dy FLOAT
);
```


## Inference Result 

| 字段           | 类型 | 描述                  |
| -------------- | ---- | --------------------- |
| rtsp_id        |      | RTSP 流唯一 ID 标识   |
| inf_res        |      | 推理结果              |
| keywords       |      | 关键字                |
| status         |      | 结果状态（异常，正常）|
| inf_timestamp  |      | 推理时间点            |
| warning_status |      | 警报推送状态          |
| warning_msg    |      | 警报推送消息          |


