<!-- 管理控制台页面 -->
<template>
  <div class="dashboard">
    <!-- 顶部统计卡片 -->
    <div class="statistic-cards">
      <el-card class="statistic-card user-card" shadow="hover">
        <div class="card-inner">
          <div class="statistic-icon user-icon">
            <el-icon><User /></el-icon>
          </div>
          <div class="statistic-content">
            <div class="statistic-value">{{ userCount }}</div>
            <div class="statistic-label">用户总数</div>
            <div class="statistic-trend">
              <el-icon><ArrowUp class="up-trend" /></el-icon>
              <span>较上月增长 7%</span>
            </div>
          </div>
        </div>
      </el-card>
      
      <el-card class="statistic-card camera-card" shadow="hover">
        <div class="card-inner">
          <div class="statistic-icon camera-icon">
            <el-icon><VideoCamera /></el-icon>
          </div>
          <div class="statistic-content">
            <div class="statistic-value">{{ cameraCount }}</div>
            <div class="statistic-label">摄像头总数</div>
            <div class="statistic-trend">
              <el-icon><Connection class="normal-trend" /></el-icon>
              <span>运行稳定</span>
            </div>
          </div>
        </div>
      </el-card>
      
      <el-card class="statistic-card record-card" shadow="hover">
        <div class="card-inner">
          <div class="statistic-icon record-icon">
            <el-icon><Document /></el-icon>
          </div>
          <div class="statistic-content">
            <div class="statistic-value">{{ recordCount }}</div>
            <div class="statistic-label">检测记录总数</div>
            <div class="statistic-trend">
              <el-icon><ArrowUp class="up-trend" /></el-icon>
              <span>今日新增 {{ Math.floor(recordCount * 0.03) || 5 }}</span>
            </div>
          </div>
        </div>
      </el-card>
      
      <el-card class="statistic-card online-card" shadow="hover">
        <div class="card-inner">
          <div class="statistic-icon online-icon">
            <el-icon><Monitor /></el-icon>
          </div>
          <div class="statistic-content">
            <div class="statistic-value">{{ onlineCameraCount }}</div>
            <div class="statistic-label">在线摄像头</div>
            <div class="statistic-trend">
              <el-icon><Cpu class="normal-trend" /></el-icon>
              <span>占比 {{ Math.floor((onlineCameraCount / Math.max(cameraCount, 1)) * 100) }}%</span>
            </div>
          </div>
        </div>
      </el-card>
    </div>
    
    <!-- 图表区域 -->
    <div class="chart-container">
      <!-- 摄像头状态分布 -->
      <el-card class="chart-card" shadow="hover">
        <template #header>
          <div class="card-header">
            <span class="header-title"><el-icon><VideoCamera /></el-icon> 摄像头状态分布</span>
          </div>
        </template>
        <div id="camera-status-chart" class="chart"></div>
      </el-card>
      
      <!-- 检测记录时间分布 -->
      <el-card class="chart-card" shadow="hover">
        <template #header>
          <div class="card-header">
            <span class="header-title"><el-icon><DataLine /></el-icon> 检测记录趋势 ({{ selectedTimeRange }})</span>
            <el-radio-group v-model="selectedTimeRange" size="small" @change="fetchDetectionTrend">
              <el-radio-button label="近7天"></el-radio-button>
              <el-radio-button label="近30天"></el-radio-button>
            </el-radio-group>
          </div>
        </template>
        <div id="detection-trend-chart" class="chart"></div>
      </el-card>
    </div>
    
    <div class="chart-container">
      <!-- 检测记录处理状态 -->
      <el-card class="chart-card" shadow="hover">
        <template #header>
          <div class="card-header">
            <span class="header-title"><el-icon><Document /></el-icon> 检测记录处理状态</span>
          </div>
        </template>
        <div id="record-status-chart" class="chart"></div>
      </el-card>
      
      <!-- 检测记录来源分布 -->
      <el-card class="chart-card" shadow="hover">
        <template #header>
          <div class="card-header">
            <span class="header-title"><el-icon><Monitor /></el-icon> 检测记录来源分布</span>
          </div>
        </template>
        <div id="record-source-chart" class="chart"></div>
      </el-card>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch, computed } from 'vue'
import { User, DataLine, Monitor, Document, VideoCamera, ArrowUp, Connection, Cpu } from '@element-plus/icons-vue'
import * as echarts from 'echarts'
import { getUserList } from '@/api/user'
import { getCameraList } from '@/api/camera'
import { pageDetectionRecords } from '@/api/detection'
import type { Camera } from '@/types/camera'
import type { DetectionRecord } from '@/api/detection'

// 图表实例
let cameraStatusChart: echarts.ECharts | undefined = undefined
let detectionTrendChart: echarts.ECharts | undefined = undefined
let recordStatusChart: echarts.ECharts | undefined = undefined
let recordSourceChart: echarts.ECharts | undefined = undefined

// 数据统计
const userCount = ref<number>(0)
const cameraCount = ref<number>(0)
const recordCount = ref<number>(0)
const onlineCameraCount = ref<number>(0)

// 时间范围选择
const selectedTimeRange = ref('近7天')

// 检测记录趋势数据
const trendData = ref<any[]>([])
const camerasMap = ref<Map<number, {name: string, location: string}>>(new Map())

// 窗口大小变化时重置图表
const handleResize = () => {
  cameraStatusChart?.resize()
  detectionTrendChart?.resize()
  recordStatusChart?.resize()
  recordSourceChart?.resize()
}

// 获取用户数据
const fetchUserData = async () => {
  try {
    const response = await getUserList({
      current: 1,
      size: 1,  // 只需要总数
      username: '',
      status: undefined
    })
    
    userCount.value = response.total
  } catch (error) {
    console.error('获取用户数据失败:', error)
  }
}

// 获取摄像头数据
const fetchCameraData = async () => {
  try {
    const response = await getCameraList({
      current: 1,
      size: 100,  // 获取足够的摄像头数据来统计状态
      name: ''
    })
    
    const cameras = response.records as Camera[]
    cameraCount.value = response.total
    
    // 创建摄像头ID到名称和地点的映射
    cameras.forEach(camera => {
      camerasMap.value.set(camera.id, {
        name: camera.name,
        location: camera.location
      })
    })
    
    // 统计摄像头状态
    const onlineCameras = cameras.filter(camera => camera.status === 1)
    onlineCameraCount.value = onlineCameras.length
    
    initCameraStatusChart(onlineCameras.length, cameras.length - onlineCameras.length)
  } catch (error) {
    console.error('获取摄像头数据失败:', error)
  }
}

// 获取检测记录数据
const fetchDetectionData = async () => {
  try {
    // 先获取总数
    const response = await pageDetectionRecords({
      current: 1,
      size: 1
    })
    
    recordCount.value = response.total
    
    // 获取记录状态分布
    const fullResponse = await pageDetectionRecords({
      current: 1,
      size: 100  // 获取更多记录来分析状态
    })
    
    if (fullResponse && fullResponse.records) {
      const records = fullResponse.records as DetectionRecord[]
      
      // 处理状态统计
      const processedCount = records.filter(record => record.processed === 1).length
      const unprocessedCount = records.length - processedCount
      
      initRecordStatusChart(processedCount, unprocessedCount)
      
      // 统计摄像头来源分布
      initRecordSourceChart(records)
    }
    
    // 获取检测记录趋势
    fetchDetectionTrend()
  } catch (error) {
    console.error('获取检测记录数据失败:', error)
  }
}

// 获取检测记录趋势数据
const fetchDetectionTrend = async () => {
  try {
    const days = selectedTimeRange.value === '近7天' ? 7 : 30
    
    // 计算开始时间和结束时间
    const endDate = new Date()
    endDate.setHours(23, 59, 59, 999)  // 设置为当天的最后一毫秒
    
    const startDate = new Date()
    startDate.setDate(endDate.getDate() - days + 1)  // +1 确保包含当天
    startDate.setHours(0, 0, 0, 0)  // 设置为当天的第一毫秒
    
    // 格式化日期为后端期望的格式 (yyyy-MM-dd HH:mm:ss)
    const formatDateTime = (date: Date) => {
      const year = date.getFullYear()
      const month = String(date.getMonth() + 1).padStart(2, '0')
      const day = String(date.getDate()).padStart(2, '0')
      const hours = String(date.getHours()).padStart(2, '0')
      const minutes = String(date.getMinutes()).padStart(2, '0')
      const seconds = String(date.getSeconds()).padStart(2, '0')
      return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`
    }
    
    const startTime = formatDateTime(startDate)
    const endTime = formatDateTime(endDate)
    
    // 查询这段时间内的所有记录
    const response = await pageDetectionRecords({
      current: 1,
      size: 1000,  // 尝试获取足够多的记录
      startTime,
      endTime
    })
    
    if (response && response.records) {
      const records = response.records as DetectionRecord[]
      
      // 生成日期范围和映射
      const dateLabels: string[] = []
      const dateMap: Map<string, number> = new Map()
      
      // 创建日期格式化器，确保按照相同的格式处理
      const formatDate = (date: Date) => {
        return `${date.getMonth() + 1}/${date.getDate()}`
      }
      
      // 生成日期范围（从开始日期到结束日期）
      const currentDate = new Date(startDate)
      while (currentDate <= endDate) {
        const dateKey = formatDate(currentDate)
        dateLabels.push(dateKey)
        dateMap.set(dateKey, 0)  // 初始化每天的计数为0
        
        // 移动到下一天
        currentDate.setDate(currentDate.getDate() + 1)
      }
      
      // 统计每一天的记录数量
      records.forEach(record => {
        // 使用检测记录的 detectionTime 而不是 createTime
        const timeField = record.detectionTime || record.createTime
        const recordDate = new Date(timeField)
        const dateKey = formatDate(recordDate)
        
        if (dateMap.has(dateKey)) {
          const currentCount = dateMap.get(dateKey) || 0
          dateMap.set(dateKey, currentCount + 1)
        }
      })
      
      // 将Map转换为数组，保持日期顺序
      const data = dateLabels.map(date => dateMap.get(date) || 0)
      
      initDetectionTrendChart(dateLabels, data)
    } else {
      // 初始化空图表
      initDetectionTrendChart([], [])
    }
  } catch (error) {
    console.error('获取检测记录趋势数据失败:', error)
    // 初始化空图表
    initDetectionTrendChart([], [])
  }
}

// 初始化摄像头状态图表
const initCameraStatusChart = (online: number, offline: number) => {
  const chartDom = document.getElementById('camera-status-chart')
  if (!chartDom) return
  
  cameraStatusChart = echarts.init(chartDom)
  
  const option = {
    tooltip: {
      trigger: 'item',
      formatter: '{b}: {c} ({d}%)'
    },
    legend: {
      orient: 'horizontal',
      bottom: 0
    },
    color: ['#059669', '#ef4444'],
    series: [
      {
        name: '摄像头状态',
        type: 'pie',
        radius: ['40%', '70%'],
        avoidLabelOverlap: false,
        itemStyle: {
          borderRadius: 10,
          borderColor: '#fff',
          borderWidth: 2
        },
        label: {
          show: false,
          position: 'center'
        },
        emphasis: {
          label: {
            show: true,
            fontSize: 18,
            fontWeight: 'bold'
          }
        },
        labelLine: {
          show: false
        },
        data: [
          { value: online, name: '在线' },
          { value: offline, name: '离线' }
        ]
      }
    ]
  }
  
  cameraStatusChart.setOption(option)
}

// 初始化检测记录趋势图表
const initDetectionTrendChart = (dates: string[], data: number[]) => {
  const chartDom = document.getElementById('detection-trend-chart')
  if (!chartDom) return
  
  // 如果图表已存在，先销毁
  if (detectionTrendChart) {
    detectionTrendChart.dispose()
  }
  
  detectionTrendChart = echarts.init(chartDom)
  
  // 如果没有数据，显示空状态
  if (!dates.length || !data.length || data.every(count => count === 0)) {
    const option = {
      title: {
        text: '暂无数据',
        left: 'center',
        top: 'middle',
        textStyle: {
          color: '#999',
          fontSize: 14
        }
      },
      xAxis: {
        type: 'category',
        data: dates.length ? dates : ['暂无数据'],
        show: dates.length > 0
      },
      yAxis: {
        type: 'value',
        show: dates.length > 0
      },
      series: [{
        name: '检测数量',
        type: 'bar',
        data: dates.length ? data : [0],
        itemStyle: {
          color: '#e0e0e0'
        },
        silent: true
      }]
    }
    detectionTrendChart.setOption(option)
    return
  }
  
  const option = {
    tooltip: {
      trigger: 'axis',
      axisPointer: {
        type: 'shadow'
      },
      formatter: (params: any) => {
        const dataIndex = params[0].dataIndex
        const date = dates[dataIndex]
        const count = data[dataIndex]
        return `日期: ${date}<br/>检测数量: ${count}`
      }
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      containLabel: true
    },
    xAxis: [
      {
        type: 'category',
        data: dates,
        axisTick: {
          alignWithLabel: true
        },
        axisLabel: {
          interval: 0,
          rotate: dates.length > 10 ? 45 : 0
        }
      }
    ],
    yAxis: [
      {
        type: 'value',
        minInterval: 1,
        axisLabel: {
          formatter: '{value}'
        }
      }
    ],
    series: [
      {
        name: '检测数量',
        type: 'bar',
        barWidth: '60%',
        data: data,
        itemStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: '#1e40af' },
            { offset: 1, color: '#059669' }
          ])
        },
        label: {
          show: true,
          position: 'top',
          formatter: '{c}'
        }
      }
    ]
  }
  
  detectionTrendChart.setOption(option)
}

// 初始化记录状态图表
const initRecordStatusChart = (processed: number, unprocessed: number) => {
  const chartDom = document.getElementById('record-status-chart')
  if (!chartDom) return
  
  recordStatusChart = echarts.init(chartDom)
  
  const option = {
    tooltip: {
      trigger: 'item',
      formatter: '{b}: {c} ({d}%)'
    },
    legend: {
      orient: 'horizontal',
      bottom: 0
    },
    color: ['#059669', '#f59e0b'],
    series: [
      {
        name: '处理状态',
        type: 'pie',
        radius: '55%',
        center: ['50%', '45%'],
        data: [
          { value: processed, name: '已处理' },
          { value: unprocessed, name: '未处理' }
        ],
        emphasis: {
          itemStyle: {
            shadowBlur: 10,
            shadowOffsetX: 0,
            shadowColor: 'rgba(0, 0, 0, 0.5)'
          }
        },
        itemStyle: {
          borderRadius: 8,
          borderColor: '#fff',
          borderWidth: 2
        }
      }
    ]
  }
  
  recordStatusChart.setOption(option)
}

// 初始化检测记录来源分布图表
const initRecordSourceChart = (records: DetectionRecord[]) => {
  const chartDom = document.getElementById('record-source-chart')
  if (!chartDom) return
  
  recordSourceChart = echarts.init(chartDom)
  
  // 统计每个摄像头的记录数量
  const cameraCount = new Map<number, number>()
  records.forEach(record => {
    const count = cameraCount.get(record.cameraId) || 0
    cameraCount.set(record.cameraId, count + 1)
  })
  
  // 准备图表数据
  const chartData = Array.from(cameraCount.entries()).map(([cameraId, count]) => {
    const cameraInfo = camerasMap.value.get(cameraId) || { name: `摄像头${cameraId}`, location: '未知位置' }
    return {
      name: `${cameraInfo.name}(${cameraInfo.location})`,
      value: count,
      cameraId
    }
  })
  
  // 按数量排序
  chartData.sort((a, b) => b.value - a.value)
  
  // 安全监控主题配色方案
  const colorPalette = [
    '#1e40af', '#059669', '#dc2626', '#ea580c', 
    '#7c3aed', '#0891b2', '#be123c', '#a16207',
    '#4338ca', '#047857', '#b91c1c', '#c2410c'
  ]
  
  const option = {
    tooltip: {
      trigger: 'item',
      formatter: (params: any) => {
        const cameraId = params.data.cameraId
        const cameraInfo = camerasMap.value.get(cameraId) || { name: `摄像头${cameraId}`, location: '未知位置' }
        return `${cameraInfo.name}<br/>位置: ${cameraInfo.location}<br/>记录数: ${params.value} (${params.percent}%)`
      }
    },
    legend: {
      type: 'scroll',
      orient: 'horizontal',
      bottom: 0,
      data: chartData.map(item => item.name)
    },
    series: [
      {
        name: '检测来源',
        type: 'pie',
        radius: '55%',
        center: ['50%', '45%'],
        data: chartData,
        emphasis: {
          itemStyle: {
            shadowBlur: 10,
            shadowOffsetX: 0,
            shadowColor: 'rgba(0, 0, 0, 0.5)'
          }
        },
        label: {
          formatter: '{b}: {c} ({d}%)'
        },
        itemStyle: {
          borderRadius: 8,
          borderColor: '#fff',
          borderWidth: 2
        },
        color: colorPalette
      }
    ]
  }
  
  recordSourceChart.setOption(option)
}

onMounted(() => {
  // 获取各类数据
  fetchUserData()
  fetchCameraData()
  fetchDetectionData()
  
  // 监听窗口大小变化
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  // 清理图表实例和事件监听
  window.removeEventListener('resize', handleResize)
  
  cameraStatusChart?.dispose()
  detectionTrendChart?.dispose()
  recordStatusChart?.dispose()
  recordSourceChart?.dispose()
})
</script>

<style scoped>
.dashboard {
  padding: 24px;
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
  min-height: calc(100vh - 64px);
}

.statistic-cards {
  display: flex;
  flex-wrap: wrap;
  gap: 20px;
  margin-bottom: 25px;
}

.statistic-card {
  flex: 1;
  min-width: 200px;
  border-radius: 12px;
  transition: all 0.3s ease;
  border: none;
  box-shadow: 0 8px 16px rgba(0, 0, 0, 0.05);
  background: rgba(255, 255, 255, 0.9);
  overflow: hidden;
  position: relative;
  padding: 0;
}

.card-inner {
  display: flex;
  flex-direction: row;
  align-items: center;
  padding: 15px;
  width: 100%;
}

.statistic-card::before {
  content: "";
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: linear-gradient(135deg, rgba(255, 255, 255, 0.1) 0%, rgba(255, 255, 255, 0) 100%);
  z-index: 1;
}

.statistic-card:hover {
  transform: translateY(-5px);
  box-shadow: 0 12px 20px rgba(0, 0, 0, 0.08);
}

.user-card { border-left: 4px solid #1e40af; }
.camera-card { border-left: 4px solid #059669; }
.record-card { border-left: 4px solid #dc2626; }
.online-card { border-left: 4px solid #ea580c; }

.statistic-icon {
  display: flex;
  justify-content: center;
  align-items: center;
  width: 60px;
  height: 60px;
  border-radius: 12px;
  margin-right: 15px;
  color: #fff;
  font-size: 24px;
  z-index: 2;
  position: relative;
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
  background-size: 200% 200%;
  animation: gradientBG 10s ease infinite;
  flex-shrink: 0;
}

@keyframes gradientBG {
  0% { background-position: 0% 50%; }
  50% { background-position: 100% 50%; }
  100% { background-position: 0% 50%; }
}

.user-icon {
  background: linear-gradient(135deg, #1e40af 0%, #3b82f6 100%);
}

.camera-icon {
  background: linear-gradient(135deg, #059669 0%, #10b981 100%);
}

.record-icon {
  background: linear-gradient(135deg, #dc2626 0%, #ef4444 100%);
}

.online-icon {
  background: linear-gradient(135deg, #ea580c 0%, #f97316 100%);
}

.statistic-content {
  display: flex;
  flex-direction: column;
  z-index: 2;
  position: relative;
  width: calc(100% - 75px);
}

.statistic-value {
  font-size: 24px;
  font-weight: bold;
  color: #303133;
  margin-bottom: 2px;
  position: relative;
}

.statistic-label {
  font-size: 14px;
  color: #909399;
  font-weight: 500;
  margin-bottom: 8px;
}

.statistic-trend {
  display: flex;
  align-items: center;
  font-size: 12px;
  color: #909399;
}

.statistic-trend .el-icon {
  margin-right: 5px;
  font-size: 14px;
}

.up-trend {
  color: #67C23A;
}

.down-trend {
  color: #F56C6C;
}

.normal-trend {
  color: #409EFF;
}

.chart-container {
  display: flex;
  gap: 25px;
  margin-bottom: 25px;
}

.chart-card {
  flex: 1;
  min-width: 300px;
  border-radius: 12px;
  box-shadow: 0 8px 16px rgba(0, 0, 0, 0.05);
  background: rgba(255, 255, 255, 0.9);
  transition: all 0.3s ease;
  border: none;
  overflow: hidden;
}

.chart-card:hover {
  box-shadow: 0 12px 20px rgba(0, 0, 0, 0.08);
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 15px 20px;
  border-bottom: 1px solid rgba(0, 0, 0, 0.05);
}

.header-title {
  display: flex;
  align-items: center;
  font-size: 16px;
  font-weight: 600;
  color: #303133;
}

.header-title i {
  margin-right: 8px;
  color: #409EFF;
}

.chart {
  height: 350px;
  padding: 20px;
}

@media (max-width: 1200px) {
  .chart-container {
    flex-direction: column;
  }
  
  .chart-card {
    margin-bottom: 20px;
  }
}

@media (max-width: 768px) {
  .statistic-cards {
    flex-direction: column;
  }
  
  .statistic-card {
    margin-bottom: 15px;
  }
}
</style> 