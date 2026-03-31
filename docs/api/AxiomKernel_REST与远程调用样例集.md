# AxiomKernel REST与远程调用样例集

本文档给出 `AxiomKernel` 在服务化、远程建模和云端任务场景下的接口样例，目标是为后续 Web 服务、任务队列和远程自动化建模能力提供参考。

## 1. 文档目标

本文档用于说明：

- 哪些能力适合远程暴露
- 请求和响应建议怎么设计
- 如何携带诊断信息

## 2. 适合远程暴露的能力

建议优先暴露：

- 基础体构造
- 布尔操作
- 验证与修复
- 数据导入导出
- 三角化
- 查询分析

## 3. 示例接口

### 3.1 创建盒体

`POST /v1/primitives/box`

请求示例：

```json
{
  "origin": [0, 0, 0],
  "dx": 100,
  "dy": 80,
  "dz": 30
}
```

响应示例：

```json
{
  "status": "Ok",
  "bodyId": "body-1001",
  "diagnosticId": "diag-0001",
  "warnings": []
}
```

### 3.2 布尔差集

`POST /v1/booleans/subtract`

请求示例：

```json
{
  "lhs": "body-1001",
  "rhs": "body-1002",
  "autoRepair": true,
  "diagnostics": true
}
```

响应示例：

```json
{
  "status": "Ok",
  "output": "body-1003",
  "diagnosticId": "diag-0102",
  "warnings": [
    {
      "code": "AXM-BOOL-W-0001",
      "message": "检测到接近退化的布尔区域"
    }
  ]
}
```

## 4. 诊断查询接口

建议暴露：

`GET /v1/diagnostics/{id}`

响应示例：

```json
{
  "id": "diag-0102",
  "summary": "布尔操作完成，存在局部退化告警",
  "issues": [
    {
      "code": "AXM-BOOL-W-0001",
      "severity": "Warning",
      "message": "检测到接近退化的布尔区域"
    }
  ]
}
```

## 5. 结论

这份文档的重点是把本地内核接口转换成可远程调用的结果语义，而不是现在就把 `AxiomKernel` 变成服务。后续如果走云端 CAD 或自动化任务平台，这份样例可以直接作为接口原型基础。
