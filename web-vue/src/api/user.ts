import { request } from './request'
import type { UserQueryParams, UserForm, UserInfo, LoginResponse } from '@/types/user'
import type { PageVO } from '@/types/common'

// 登录
export const login = async (username: string, password: string) => {
  const res = await request.post<LoginResponse>('/user/login', { username, password })
  return res
}

// 注册
export const register = (data: {
  username: string
  password: string
  realName: string
  phone: string
  email: string
}) => {
  return request.post('/user/register', data)
}

// 退出登录
export const logout = () => {
  return request.post('/user/logout')
}

// 修改密码
export const updatePassword = (data: {
  id: number
  oldPassword: string
  newPassword: string
}) => {
  return request.post('/user/password', data)
}

// 获取用户列表
export const getUserList = (params: UserQueryParams) => {
  return request.get<PageVO<UserInfo>>('/user/page', { params })
}

// 获取用户详情
export const getUserInfo = (id: number) => {
  return request.get<UserInfo>(`/user/${id}`)
}

// 新增用户
export const addUser = (data: UserForm) => {
  return request.post<void>('/user', data)
}

// 修改用户
export const updateUser = (data: UserForm) => {
  return request.put<void>('/user', data)
}

// 删除用户
export const deleteUser = (id: number) => {
  return request.delete<void>(`/user/${id}`)
}

// 重置密码
export const resetPassword = (id: number) => {
  return request.put<void>(`/user/${id}/reset-password`)
}

// 更新用户状态
export const updateUserStatus = (data: { id: number; status: number }) => {
  return request.put<void>(`/user/${data.id}/status`, { status: data.status })
} 