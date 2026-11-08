const CONSENT_VERSION = 1
const CONSENT_KEY = 'disclaimer_accepted_version'
const LOCAL_KEY = 'ssmt4:liquid-gravity-probe:disclaimer-version'
let sequence = 0

interface CapabilityResponse {
  type?: string
  protocolVersion?: number
  requestId?: string
  ok?: boolean
  value?: unknown
  errorCode?: string
  errorMessage?: string
}

const normalizeVersion = (value: unknown) => {
  const parsed = typeof value === 'number' ? value : Number(value)
  return Number.isFinite(parsed) ? parsed : 0
}

async function requestCapability(capability: string, payload: Record<string, unknown>) {
  const requestId = `lgp-consent-${Date.now()}-${++sequence}`
  return new Promise<CapabilityResponse>((resolve, reject) => {
    const timer = window.setTimeout(() => {
      window.removeEventListener('message', receive)
      reject(new Error('插件能力桥超时'))
    }, 5000)
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
}

export async function hasConsent(): Promise<boolean> {
  if (normalizeVersion(localStorage.getItem(LOCAL_KEY)) >= CONSENT_VERSION) return true
  try {
    const response = await requestCapability('plugin.settings.get', { key: CONSENT_KEY })
    const stored = response.ok
      ? (response.value as { value?: unknown } | null)?.value
      : undefined
    if (normalizeVersion(stored) >= CONSENT_VERSION) {
      localStorage.setItem(LOCAL_KEY, String(CONSENT_VERSION))
      return true
    }
  } catch {
    // The local fallback remains available when the host bridge is unavailable.
  }
  return false
}

export async function acceptConsent(): Promise<void> {
  localStorage.setItem(LOCAL_KEY, String(CONSENT_VERSION))
  const response = await requestCapability('plugin.settings.set', {
    key: CONSENT_KEY,
    value: CONSENT_VERSION,
  })
  if (!response.ok) throw new Error(response.errorMessage || response.errorCode || '无法保存同意状态')
}

export async function declineConsent(): Promise<void> {
  localStorage.removeItem(LOCAL_KEY)
  await requestCapability('plugin.settings.set', {
    key: CONSENT_KEY,
    value: 0,
  }).catch(() => undefined)
}
