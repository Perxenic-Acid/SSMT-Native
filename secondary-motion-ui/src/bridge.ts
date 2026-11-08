export const DATA_ID = 'SecondaryMotion'

interface CapabilityResponse {
  type?: string
  protocolVersion?: number
  requestId?: string
  ok?: boolean
  value?: unknown
  errorCode?: string
  errorMessage?: string
}

let sequence = 0

export async function requestCapability(
  capability: string,
  payload: Record<string, unknown>,
): Promise<unknown> {
  const requestId = `secondary-motion-${Date.now()}-${++sequence}`
  const response = await new Promise<CapabilityResponse>((resolve, reject) => {
    const timer = window.setTimeout(() => {
      window.removeEventListener('message', receive)
      reject(new Error('插件能力桥超时'))
    }, 8000)
    function receive(event: MessageEvent<CapabilityResponse>) {
      if (event.source !== window.parent
        || event.data?.type !== 'ssmt:capability-response'
        || event.data?.protocolVersion !== 1
        || event.data?.requestId !== requestId) return
      window.clearTimeout(timer)
      window.removeEventListener('message', receive)
      resolve(event.data)
    }
    window.addEventListener('message', receive)
    window.parent.postMessage({
      type: 'ssmt:capability-request',
      protocolVersion: 1,
      requestId,
      capability,
      payload,
    }, '*')
  })
  if (!response.ok) {
    throw new Error(response.errorMessage || response.errorCode || '插件能力调用失败')
  }
  return response.value
}

export async function readData(path: string): Promise<string> {
  const value = await requestCapability('plugin.data.read', { dataId: DATA_ID, path }) as {
    content?: string
  }
  return value.content ?? ''
}

export async function readRuntimeData(path: string): Promise<string> {
  const value = await requestCapability('plugin.data.runtime.read', {
    dataId: DATA_ID,
    path,
  }) as { content?: string }
  return value.content ?? ''
}

export async function writeData(path: string, content: string): Promise<void> {
  await requestCapability('plugin.data.write', {
    dataId: DATA_ID,
    path,
    content,
  })
}

export async function listData(path: string): Promise<Array<{
  path: string
  name: string
  kind: string
  size: number
}>> {
  const value = await requestCapability('plugin.data.list', {
    dataId: DATA_ID,
    path,
  }) as { entries?: Array<{ path: string; name: string; kind: string; size: number }> }
  return value.entries ?? []
}

export async function publishData(): Promise<void> {
  await requestCapability('plugin.data.publish', { dataId: DATA_ID })
}

export async function gameProcessRunning(): Promise<boolean> {
  const value = await requestCapability('game.process.read', {}) as { running?: boolean }
  return value.running === true
}
