import { shallowRef } from 'vue'

let sequence = 0
const pending = new Map<string, { resolve: (value: any) => void; reject: (error: Error) => void; timer: ReturnType<typeof setTimeout> }>()
window.addEventListener('message', event => {
  if (event.source !== parent || event.data?.type !== 'ssmt:capability-response' || event.data.protocolVersion !== 1) return
  const task = pending.get(event.data.requestId)
  if (!task) return
  clearTimeout(task.timer)
  pending.delete(event.data.requestId)
  event.data.ok ? task.resolve(event.data.value) : task.reject(Error(event.data.errorMessage || '插件调用失败'))
})
export function capability<T>(capability: string, payload: unknown): Promise<T> {
  return new Promise((resolve, reject) => {
    const requestId = `dlss5-${++sequence}`
    const timer = setTimeout(() => { pending.delete(requestId); reject(Error('宿主响应超时')) }, 125000)
    pending.set(requestId, { resolve, reject, timer })
    parent.postMessage({ type: 'ssmt:capability-request', protocolVersion: 1, requestId, capability, payload }, '*')
  })
}
const call = <T>(method: string, payload: unknown): Promise<T> => capability('plugin.backend.request', { method, payload })
export class Channel<T> { onmessage: (value: T) => void = () => {} }
interface Task { taskId: string; status: string; cancellable: boolean; progress: unknown; result: unknown; error?: string }
export const activeTask = shallowRef<Task | null>(null)
export async function restoreActiveTask(): Promise<void> {
  const tasks = await call<Task[]>('task.active', {})
  if (!tasks.length) return
  activeTask.value = tasks[0]
  try {
    while (activeTask.value && ['queued', 'running'].includes(activeTask.value.status)) {
      await new Promise(resolve => setTimeout(resolve, 500))
      activeTask.value = await call<Task>('task.read', { taskId: activeTask.value.taskId })
    }
  } finally { activeTask.value = null }
}
export async function cancelActiveTask() {
  if (activeTask.value?.cancellable) await call('task.cancel', { taskId: activeTask.value.taskId })
}
const operations: Record<string, string> = {
  download_gimi_hot_runtime: 'runtime.download', download_dlss5_hot_runtime: 'runtime.download',
  import_gimi_hot_runtime: 'runtime.import', select_dlss5_hot_runtime: 'runtime.select',
  remove_dlss5_hot_runtime: 'runtime.remove', install_managed_dlss5_route: 'route.install', restore_dlss5_install: 'route.restore',
}
export async function invoke<T = unknown>(method: string, args: Record<string, any>): Promise<T> {
  const operation = operations[method]
  if (!operation) return call<T>(method, args)
  if (activeTask.value) throw Error('已有组件任务正在执行')
  const { onProgress, ...payload } = args
  let task = await call<Task>('task.start', { ...payload, operation, runtime: method === 'download_gimi_hot_runtime' ? 'gimi' : 'dlss5' })
  activeTask.value = task
  try {
    const deadline = Date.now() + 31 * 60 * 1000
    while (task.status === 'queued' || task.status === 'running') {
      if (Date.now() > deadline) throw Error('组件任务超时，请重新打开页面检查任务状态')
      await new Promise(resolve => setTimeout(resolve, 300))
      task = await call<Task>('task.read', { taskId: task.taskId })
      activeTask.value = task
      if (task.progress) onProgress?.onmessage(task.progress)
    }
    if (task.status !== 'succeeded') throw Error(task.error || task.status)
    return task.result as T
  } finally { activeTask.value = null }
}
export const open = async (_options: unknown): Promise<string | null> => (await capability<{path: string | null}>('host.directory.select', {})).path
export const openUrl = (url: string): Promise<void> => capability('host.url.open', { url })
