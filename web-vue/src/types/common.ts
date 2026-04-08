// 分页查询参数
export interface PageDTO {
  current: number // 当前页码
  size: number // 每页大小
}

// 分页返回结果
export interface PageVO<T> {
  records: T[] // 数据列表
  total: number // 总条数
  size: number // 每页大小
  current: number // 当前页码
  pages: number // 总页数
}

// 通用响应结果
export interface Result<T> {
  code: number // 状态码
  msg: string // 消息
  data: T // 数据
}