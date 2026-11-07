export interface RuntimeConfig {
  revision: number
  enabled: boolean
  enabled_user_set: boolean
  active_preset: string
  [key: string]: unknown
}

const nonNegativeRevision = (value: unknown) => {
  const revision = Number(value)
  return Number.isFinite(revision) && revision >= 0 ? Math.floor(revision) : 0
}

const presetName = (value: unknown, fallback: string) =>
  typeof value === 'string' && value.trim() ? value : fallback

export function normalizeRuntimeConfig(
  value: unknown,
  fallbackPreset = 'Default',
): { config: RuntimeConfig; migrated: boolean } {
  const raw = value && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown>
    : {}
  const explicitPreference = raw.enabled_user_set === true
  const migrated = !explicitPreference && raw.enabled !== true
  return {
    config: {
      ...raw,
      revision: nonNegativeRevision(raw.revision) + (migrated ? 1 : 0),
      enabled: explicitPreference ? raw.enabled === true : true,
      enabled_user_set: explicitPreference,
      active_preset: presetName(raw.active_preset, fallbackPreset),
    },
    migrated,
  }
}

export function setRuntimeEnabledPreference(
  config: RuntimeConfig,
  enabled: boolean,
): RuntimeConfig {
  return {
    ...config,
    revision: nonNegativeRevision(config.revision) + 1,
    enabled,
    enabled_user_set: true,
  }
}

export function applyRuntimeSettings(
  config: RuntimeConfig,
  enabled: boolean,
  activePreset: string,
): RuntimeConfig {
  return {
    ...config,
    revision: nonNegativeRevision(config.revision) + 1,
    enabled,
    active_preset: presetName(activePreset, config.active_preset),
  }
}

export function serializeRuntimeConfig(config: RuntimeConfig): string {
  return `${JSON.stringify(config, null, 2)}\n`
}
