<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import {
  gameProcessRunning,
  listData,
  publishData,
  readData,
  readRuntimeData,
  writeData,
} from './bridge'
import {
  applyRuntimeSettings,
  normalizeRuntimeConfig,
  serializeRuntimeConfig,
  setRuntimeEnabledPreference,
} from './runtimeConfig'

interface RuntimeStatus {
  state?: string
  character?: string
  profile?: boolean
  bones?: boolean
  mode?: string
  gait?: string
  last_write_ms?: number
  error?: string
}

interface SecondaryMotionSnapshot {
  pluginId: string
  pluginName: string
  pluginVersion: string
  gameName: string
  ssmtEnabled: boolean
  runtimeEnabled: boolean
  revision: number
  activePreset: string
  presets: string[]
  dataRoot: string
  status: RuntimeStatus
  gameRunning: boolean
  characterId: string
  characterDisplayName: string
  characterInPreset: boolean
  characterSettings: Record<string, any>
}

interface ParameterField {
  path: string
  key: string
  value: string | number | boolean
}

interface ParameterDefinition extends ParameterField {
  label: string
  hint: string
}

interface GaitColumn {
  key: string
  label: string
}

interface GaitRow {
  key: string
  label: string
  values: Record<string, string | number | boolean>
}

const props = defineProps<{
  gameName: string
}>()

const isZh = computed(() => navigator.language.toLowerCase().startsWith('zh'))
const label = (zh: string, en: string) => isZh.value ? zh : en

const snapshot = ref<SecondaryMotionSnapshot | null>(null)
const runtimeEnabled = ref(true)
const activePreset = ref('')
const loading = ref(false)
const saving = ref(false)
const runtimeSaving = ref(false)
const characterSaving = ref(false)
const error = ref('')
const characterDraft = ref<Record<string, any> | null>(null)
let requestId = 0

const statusState = computed(() => {
  if (snapshot.value && !snapshot.value.gameRunning) return 'STOPPED'
  return (snapshot.value?.status?.state || 'NOT_STARTED').toUpperCase()
})
const statusLabel = computed(() => {
  const labels: Record<string, [string, string]> = {
    NOT_STARTED: ['尚未启动', 'Not started'],
    STOPPED: ['游戏未运行', 'Game not running'],
    STARTING: ['正在启动', 'Starting'],
    CONFIG_LOADED: ['配置已加载', 'Config loaded'],
    SYMBOLS_RESOLVED: ['符号已解析', 'Symbols resolved'],
    HOOKS_INSTALLED: ['Hook 已安装', 'Hooks installed'],
    READY: ['运行中', 'Ready'],
    DEGRADED: ['已运行（降级）', 'Running degraded'],
    DISABLED_SAFE: ['已安全停用', 'Safely disabled'],
  }
  const current = labels[statusState.value] || [statusState.value, statusState.value]
  return isZh.value ? current[0] : current[1]
})
const statusTone = computed(() => {
  if (statusState.value === 'READY') return 'ready'
  if (statusState.value === 'DEGRADED') return 'warning'
  if (statusState.value === 'DISABLED_SAFE') return 'danger'
  return 'idle'
})
const statusDetails = computed(() => {
  const status = snapshot.value?.status
  if (!status) return []
  return [
    status.character ? `${label(snapshot.value?.gameRunning ? '角色' : '上次角色', snapshot.value?.gameRunning ? 'Character' : 'Last character')}: ${status.character}` : '',
    status.mode ? `${label('模式', 'Mode')}: ${status.mode}` : '',
    status.gait ? `${label('步态', 'Gait')}: ${status.gait}` : '',
    typeof status.bones === 'boolean' ? `${label(snapshot.value?.gameRunning ? '骨骼' : '上次骨骼', snapshot.value?.gameRunning ? 'Bones' : 'Last bones')}: ${status.bones ? 'PASS' : 'FAIL'}` : '',
    typeof status.profile === 'boolean' ? `${label(snapshot.value?.gameRunning ? '角色配置' : '上次角色配置', snapshot.value?.gameRunning ? 'Profile' : 'Last profile')}: ${status.profile ? 'PASS' : 'FAIL'}` : '',
  ].filter(Boolean)
})
const formatFieldLabel = (path: string) => {
  const key = path.split('.').pop() || path
  const labels: Record<string, [string, string]> = {
    enabled: ['启用', 'Enabled'],
    motion_mode: ['运动模式', 'Motion mode'],
    right: ['右侧骨骼', 'Right bone'],
    left: ['左侧骨骼', 'Left bone'],
    allow_fallback_candidates: ['允许候选骨骼回退', 'Allow bone fallback'],
    name: ['轴向', 'Axis'],
    sign: ['方向符号', 'Direction sign'],
    amplitude_deg: ['幅度（度）', 'Amplitude (deg)'],
    amplitude_down_deg: ['下半周期幅度（度）', 'Down amplitude (deg)'],
    frequency_hz: ['频率（Hz）', 'Frequency (Hz)'],
    phase_offset_deg: ['相位偏移（度）', 'Phase offset (deg)'],
    amplitude_attack_tau_sec: ['幅度平滑时间（秒）', 'Amplitude tau (s)'],
    frequency_tau_sec: ['频率平滑时间（秒）', 'Frequency tau (s)'],
    to_idle_release_tau_sec: ['回待机平滑时间（秒）', 'Idle release tau (s)'],
    mode: ['模式', 'Mode'],
    takeoff_delay_sec: ['起跳延迟（秒）', 'Takeoff delay (s)'],
    rising_target_deg: ['上升期目标（度）', 'Rising target (deg)'],
    apex_falling_target_deg: ['高点/下落目标（度）', 'Apex/falling target (deg)'],
    acceleration_response: ['加速度响应', 'Acceleration response'],
    acceleration_filter_tau_sec: ['加速度滤波（秒）', 'Acceleration filter (s)'],
    natural_frequency_hz: ['弹簧频率（Hz）', 'Spring frequency (Hz)'],
    damping_ratio: ['阻尼比', 'Damping ratio'],
    landing_impulse_gain: ['落地冲击增益', 'Landing impulse gain'],
    max_duration_sec: ['最大持续时间（秒）', 'Max duration (s)'],
    factor: ['原生增益倍率', 'Native amplification factor'],
    idle: ['待机', 'Idle'],
    walk: ['行走', 'Walk'],
    run: ['奔跑', 'Run'],
    sprint: ['冲刺', 'Sprint'],
    zipline: ['滑索', 'Zipline'],
    damping_tau_sec: ['阻尼时间（秒）', 'Damping tau (s)'],
    auto_frequency: ['自动频率', 'Auto frequency'],
    phase_align: ['相位对齐', 'Phase align'],
    frequency_deviation_threshold: ['频率偏差阈值', 'Frequency deviation threshold'],
  }
  const labelValue = labels[key]
  return labelValue ? label(labelValue[0], labelValue[1]) : key
}
const formatFieldHint = (key: string) => {
  const hints: Record<string, [string, string]> = {
    amplitude_attack_tau_sec: ['动作切换时平滑幅度，数值越高越柔和。', 'Smooths amplitude when motion changes. Higher is gentler.'],
    frequency_tau_sec: ['平滑频率变化，数值越高变化越慢。', 'Smooths frequency changes. Higher changes more slowly.'],
    to_idle_release_tau_sec: ['停止动作回到待机时的专用平滑时间。', 'Dedicated smoothing when motion returns to idle.'],
    rising_target_deg: ['跳跃上升阶段的目标角度。', 'Target angle while rising during a jump.'],
    apex_falling_target_deg: ['到达高点并开始下落时的目标角度。', 'Target angle at the apex and while falling.'],
    acceleration_response: ['对垂直加速度变化的响应强度。', 'Response strength to vertical acceleration changes.'],
    natural_frequency_hz: ['弹簧模型的固有频率。', 'Natural frequency of the spring model.'],
    damping_ratio: ['弹簧阻尼比，越高越不容易持续晃动。', 'Spring damping ratio. Higher settles faster.'],
    landing_impulse_gain: ['落地冲击产生的额外摆幅。', 'Extra swing produced by the landing impulse.'],
  }
  const hintValue = hints[key]
  return hintValue ? label(hintValue[0], hintValue[1]) : ''
}
const isPrimitive = (value: unknown): value is string | number | boolean =>
  value !== null && value !== undefined && typeof value !== 'object'
const isRecord = (value: unknown): value is Record<string, unknown> =>
  Boolean(value) && typeof value === 'object' && !Array.isArray(value)
const collectPrimitiveFields = (
  value: unknown,
  prefix: string,
  target: ParameterField[] = [],
) => {
  if (!isRecord(value)) {
    if (isPrimitive(value)) {
      target.push({
        path: prefix,
        key: prefix.split('.').pop() || prefix,
        value,
      })
    }
    return target
  }
  for (const [key, child] of Object.entries(value)) {
    collectPrimitiveFields(child, prefix ? `${prefix}.${key}` : key, target)
  }
  return target
}
const withFieldMetadata = (fields: ParameterField[]): ParameterDefinition[] =>
  fields.map(field => ({
    ...field,
    label: formatFieldLabel(field.path),
    hint: formatFieldHint(field.key),
  }))
const fieldOrder = [
  'enabled',
  'motion_mode',
  'right',
  'left',
  'allow_fallback_candidates',
  'name',
  'sign',
  'amplitude_attack_tau_sec',
  'frequency_tau_sec',
  'to_idle_release_tau_sec',
  'factor',
]
const sortFields = (fields: ParameterDefinition[]) => [...fields].sort((left, right) => {
  const leftIndex = fieldOrder.indexOf(left.key)
  const rightIndex = fieldOrder.indexOf(right.key)
  if (leftIndex === -1 && rightIndex === -1) return left.path.localeCompare(right.path)
  if (leftIndex === -1) return 1
  if (rightIndex === -1) return -1
  return leftIndex - rightIndex
})

const generalFields = computed(() => sortFields(withFieldMetadata(
  Object.entries(characterDraft.value ?? {})
    .filter(([, value]) => isPrimitive(value))
    .map(([key, value]) => ({ path: key, key, value: value as string | number | boolean })),
)))
const boneFields = computed(() => sortFields(withFieldMetadata(
  collectPrimitiveFields(characterDraft.value?.bones, 'bones'),
)))
const axisFields = computed(() => sortFields(withFieldMetadata(
  collectPrimitiveFields(characterDraft.value?.axis, 'axis'),
)))
const envelopeFields = computed(() => sortFields(withFieldMetadata(
  collectPrimitiveFields(characterDraft.value?.envelope, 'envelope'),
)))
const nativeFields = computed(() => sortFields(withFieldMetadata(
  collectPrimitiveFields(characterDraft.value?.native_amplify, 'native_amplify'),
)))
const jumpFields = computed(() => sortFields(withFieldMetadata(
  collectPrimitiveFields(characterDraft.value?.jump, 'jump'),
)))
const gaitRows = computed<GaitRow[]>(() => {
  const gait = characterDraft.value?.gait
  if (!isRecord(gait)) return []
  return Object.entries(gait)
    .filter((entry): entry is [string, Record<string, string | number | boolean>] =>
      isRecord(entry[1])
      && Object.values(entry[1]).every(isPrimitive),
    )
    .map(([key, values]) => ({
      key,
      label: formatFieldLabel(`gait.${key}`),
      values,
    }))
})
const gaitColumns = computed<GaitColumn[]>(() => {
  const keys = new Set<string>()
  for (const row of gaitRows.value) {
    for (const key of Object.keys(row.values)) keys.add(key)
  }
  const order = [
    'amplitude_deg',
    'amplitude_down_deg',
    'frequency_hz',
    'phase_offset_deg',
    'phase_align',
    'auto_frequency',
    'frequency_deviation_threshold',
  ]
  return [...keys]
    .sort((left, right) => {
      const leftIndex = order.indexOf(left)
      const rightIndex = order.indexOf(right)
      if (leftIndex === -1 && rightIndex === -1) return left.localeCompare(right)
      if (leftIndex === -1) return 1
      if (rightIndex === -1) return -1
      return leftIndex - rightIndex
    })
    .map(key => ({ key, label: formatFieldLabel(`gait.${key}`) }))
})
const knownTopLevelGroups = new Set(['bones', 'axis', 'gait', 'envelope', 'jump', 'native_amplify'])
const otherGroups = computed(() => {
  const draft = characterDraft.value
  if (!isRecord(draft)) return []
  return Object.entries(draft)
    .filter(([key, value]) => !knownTopLevelGroups.has(key) && isRecord(value))
    .map(([key]) => ({
      key,
      label: formatFieldLabel(key),
      fields: sortFields(withFieldMetadata(collectPrimitiveFields(draft[key], key))),
    }))
    .filter(group => group.fields.length > 0)
})
const editorDisabled = computed(() => !snapshot.value?.characterId)
const setPathValue = (path: string, value: string | number | boolean) => {
  if (!characterDraft.value) return
  const parts = path.split('.')
  let target: Record<string, any> = characterDraft.value
  for (const part of parts.slice(0, -1)) {
    if (!target[part] || typeof target[part] !== 'object') target[part] = {}
    target = target[part]
  }
  target[parts[parts.length - 1]] = value
}

const parseJson = (value: string, fallback: any) => {
  try { return JSON.parse(value) } catch { return fallback }
}

const mergeJson = (base: any, overlay: any) => {
  if (base && overlay && typeof base === 'object' && typeof overlay === 'object'
    && !Array.isArray(base) && !Array.isArray(overlay)) {
    for (const [key, value] of Object.entries(overlay)) {
      base[key] = key in base ? mergeJson(base[key], value) : structuredClone(value)
    }
    return base
  }
  return structuredClone(overlay)
}

const characterDefaults = (database: any, preset: any, characterId: string) => {
  const defaults: any = {
    enabled: true,
    motion_mode: 'synthetic',
    bones: { right: '', left: '', allow_fallback_candidates: true },
    axis: { name: 'Y', sign: 1 },
    gait: {
      idle: { amplitude_deg: 0, frequency_hz: 1.2 },
      walk: { amplitude_deg: 3.6, frequency_hz: 1.5 },
      run: { amplitude_deg: 8.5, frequency_hz: 1.7, phase_offset_deg: 180 },
      sprint: { amplitude_deg: 12, frequency_hz: 2, phase_offset_deg: 180 },
      zipline: { amplitude_deg: 8.5, frequency_hz: 1.7 },
    },
    envelope: {
      amplitude_attack_tau_sec: 0.15,
      frequency_tau_sec: 0.2,
      to_idle_release_tau_sec: 0.02,
    },
    jump: {
      enabled: false, mode: 'off', amplitude_deg: 23.333, damping_tau_sec: 0.35,
      frequency_hz: 3, max_duration_sec: 1.2, takeoff_delay_sec: 0.08,
      rising_target_deg: -23.333, apex_falling_target_deg: 23.333,
      acceleration_response: 0.1667, acceleration_filter_tau_sec: 0.08,
      natural_frequency_hz: 2.2, damping_ratio: 0.52, landing_impulse_gain: 6.667,
    },
    native_amplify: { factor: 2 },
  }
  const databaseEntry = database?.characters?.[characterId]
  if (databaseEntry?.bones) defaults.bones = structuredClone(databaseEntry.bones)
  if (databaseEntry?.axis) defaults.axis = structuredClone(databaseEntry.axis)
  for (const key of ['gait', 'envelope', 'jump', 'native_amplify']) {
    if (databaseEntry?.defaults?.[key]) defaults[key] = structuredClone(databaseEntry.defaults[key])
  }
  const presetEntry = preset?.characters?.[characterId]
  return {
    present: Boolean(presetEntry),
    settings: presetEntry ? mergeJson(defaults, presetEntry) : defaults,
  }
}

const loadPresetNames = async () => {
  const entries = await listData('presets')
  return entries
    .filter(entry => entry.kind === 'file'
      && entry.name.toLowerCase().endsWith('.json')
      && !entry.name.toLowerCase().endsWith('.template.json')
      && !entry.name.toLowerCase().endsWith('.json.bak'))
    .map(entry => entry.name.slice(0, -5))
    .sort((left, right) => left.localeCompare(right))
}

const buildSnapshot = async (): Promise<SecondaryMotionSnapshot> => {
  const presets = await loadPresetNames()
  const normalized = normalizeRuntimeConfig(
    parseJson(await readData('runtime/config.json').catch(() => '{}'), {}),
    presets[0] || 'Default',
  )
  const config = normalized.config
  if (normalized.migrated) {
    await writeData('runtime/config.json', serializeRuntimeConfig(config))
    await publishData()
  }
  const active = String(config.active_preset || presets[0] || '')
  const preset = active
    ? parseJson(await readData(`presets/${active}.json`).catch(() => '{}'), {})
    : {}
  const database = parseJson(
    await readData('data/characters.default.json').catch(() => '{}'),
    {},
  )
  const status = parseJson(
    await readRuntimeData('runtime/runtime_status.json').catch(() => '{}'),
    { state: 'NOT_STARTED' },
  )
  const characterId = typeof status.character === 'string' ? status.character : ''
  const character = characterDefaults(database, preset, characterId)
  return {
    pluginId: 'ssmt.secondary-motion',
    pluginName: 'Secondary Motion',
    pluginVersion: '3.3.4',
    gameName: props.gameName,
    ssmtEnabled: true,
    runtimeEnabled: config.enabled,
    revision: Number(config.revision) || 0,
    activePreset: active,
    presets,
    dataRoot: 'PluginData/ssmt.secondary-motion/SecondaryMotion',
    status,
    gameRunning: await gameProcessRunning().catch(() => false),
    characterId,
    characterDisplayName: typeof database?.characters?.[characterId]?.display_name === 'string'
      ? database.characters[characterId].display_name
      : characterId,
    characterInPreset: character.present,
    characterSettings: character.settings,
  }
}

const load = async () => {
  if (!props.gameName) return
  const currentRequest = ++requestId
  loading.value = true
  error.value = ''
  try {
    const value = await buildSnapshot()
    if (currentRequest !== requestId) return
    snapshot.value = value
    runtimeEnabled.value = value.runtimeEnabled
    activePreset.value = value.activePreset || value.presets[0] || ''
    characterDraft.value = structuredClone(value.characterSettings ?? {})
  } catch (cause) {
    if (currentRequest === requestId) error.value = String(cause)
  } finally {
    if (currentRequest === requestId) loading.value = false
  }
}

const apply = async () => {
  if (!snapshot.value || saving.value) return
  if (!activePreset.value) {
    ElMessage.warning(label('请选择预设', 'Select a preset'))
    return
  }
  saving.value = true
  try {
    const current = normalizeRuntimeConfig(
      parseJson(await readData('runtime/config.json').catch(() => '{}'), {}),
      activePreset.value || 'Default',
    ).config
    const updated = applyRuntimeSettings(
      current,
      runtimeEnabled.value,
      activePreset.value,
    )
    await writeData('runtime/config.json', serializeRuntimeConfig(updated))
    await publishData()
    const value = await buildSnapshot()
    snapshot.value = value
    runtimeEnabled.value = value.runtimeEnabled
    activePreset.value = value.activePreset
    characterDraft.value = structuredClone(value.characterSettings ?? {})
    ElMessage.success(label(
      `配置已应用，revision ${value.revision}。游戏中会自动热加载。`,
      `Settings applied at revision ${value.revision}. The game hot-reloads them automatically.`,
    ))
  } catch (cause) {
    ElMessage.error(label(`应用失败：${String(cause)}`, `Apply failed: ${String(cause)}`))
  } finally {
    saving.value = false
  }
}

const saveRuntimeEnabled = async (value: boolean | string | number) => {
  const enabled = Boolean(value)
  const previous = snapshot.value?.runtimeEnabled ?? !enabled
  runtimeEnabled.value = enabled
  if (!snapshot.value || runtimeSaving.value) return
  runtimeSaving.value = true
  error.value = ''
  try {
    const current = normalizeRuntimeConfig(
      parseJson(await readData('runtime/config.json').catch(() => '{}'), {}),
      activePreset.value || 'Default',
    ).config
    const updated = setRuntimeEnabledPreference(current, enabled)
    await writeData('runtime/config.json', serializeRuntimeConfig(updated))
    await publishData()
    snapshot.value = {
      ...snapshot.value,
      runtimeEnabled: updated.enabled,
      revision: updated.revision,
    }
    ElMessage.success(label(
      `骨骼物理已${enabled ? '开启' : '关闭'}，revision ${updated.revision}。`,
      `Bone physics ${enabled ? 'enabled' : 'disabled'} at revision ${updated.revision}.`,
    ))
  } catch (cause) {
    runtimeEnabled.value = previous
    error.value = String(cause)
    ElMessage.error(label(`保存总开关失败：${String(cause)}`, `Failed to save motion switch: ${String(cause)}`))
  } finally {
    runtimeSaving.value = false
  }
}

const saveCharacter = async () => {
  if (!snapshot.value || !characterDraft.value || !snapshot.value.characterId || characterSaving.value) return
  characterSaving.value = true
  try {
    const characterId = snapshot.value.characterId
    if (!/^chr_[A-Za-z0-9_-]+$/.test(characterId)) throw new Error('当前角色 ID 无效')
    const presetPath = `presets/${snapshot.value.activePreset}.json`
    const preset = parseJson(await readData(presetPath), {})
    await writeData(`${presetPath}.bak`, `${JSON.stringify(preset, null, 2)}\n`)
    preset.characters = preset.characters && typeof preset.characters === 'object'
      ? preset.characters
      : {}
    preset.characters[characterId] = structuredClone(characterDraft.value)
    await writeData(presetPath, `${JSON.stringify(preset, null, 2)}\n`)
    const config = normalizeRuntimeConfig(
      parseJson(await readData('runtime/config.json').catch(() => '{}'), {}),
      snapshot.value.activePreset || 'Default',
    )
    const updated = applyRuntimeSettings(
      config.config,
      config.config.enabled,
      snapshot.value.activePreset,
    )
    await writeData('runtime/config.json', serializeRuntimeConfig(updated))
    await publishData()
    const value = await buildSnapshot()
    snapshot.value = value
    runtimeEnabled.value = value.runtimeEnabled
    activePreset.value = value.activePreset
    characterDraft.value = structuredClone(value.characterSettings ?? {})
    ElMessage.success(label(
      `角色参数已保存，revision ${value.revision}。`,
      `Character settings saved at revision ${value.revision}.`,
    ))
  } catch (cause) {
    ElMessage.error(label(`角色参数保存失败：${String(cause)}`, `Failed to save character settings: ${String(cause)}`))
  } finally {
    characterSaving.value = false
  }
}

watch(() => [props.gameName], () => void load(), { immediate: true })
onMounted(() => {
  if (!snapshot.value && !loading.value) void load()
})
</script>

<template>
  <section class="secondary-motion-settings">
    <header class="page-header">
      <div>
        <span class="eyebrow">SECONDARY MOTION</span>
        <h1>{{ label('次级身体物理', 'Secondary body physics') }}</h1>
        <p>
          {{ label(
            '按角色调整步态、跳跃惯性和骨骼参数。SSMT 直接注入包内 sbm.dll，游戏内自动热加载。',
            'Tune gait, jump inertia, and bone settings per character. SSMT injects the packaged sbm.dll and hot-reloads changes.',
          ) }}
        </p>
      </div>
      <label class="global-switch">
        <span>
          <strong>{{ label('骨骼物理总开关', 'Global motion switch') }}</strong>
          <small>{{ runtimeEnabled ? label('已开启', 'Enabled') : label('已关闭', 'Disabled') }}</small>
        </span>
        <el-switch
          v-model="runtimeEnabled"
          :loading="runtimeSaving"
          :disabled="saving"
          @change="saveRuntimeEnabled"
        />
      </label>
    </header>

    <details class="risk-notice" role="note">
      <summary>
        <span>{{ label('⚠ 原生注入风险与免责声明', '⚠ Native injection risk and disclaimer') }}</span>
        <small>{{ label('点击展开', 'Expand') }}</small>
      </summary>
      <div class="risk-body">
        <p>
          {{ label(
            '本插件会向游戏进程加载第三方 sbm.dll，并使用 IL2CPP/MinHook 修改 Unity 骨骼更新。它不是官方功能，也未获得开发商或发行商认可。',
            'This plugin loads third-party sbm.dll into the game process and uses IL2CPP/MinHook to modify Unity bone updates. It is unofficial and not endorsed by the developer or publisher.',
          ) }}
        </p>
        <ul>
          <li>{{ label('可能触发反作弊、账号风控或其他账号处置，风险由使用者自行承担。', 'It may trigger anti-cheat or account risk controls. You accept this risk.') }}</li>
          <li>{{ label('可能造成游戏崩溃、卡顿、骨骼异常、穿模或存档相关风险。', 'It may cause crashes, stutter, broken bones, clipping, or save-related risk.') }}</li>
          <li>{{ label('游戏更新可能使插件失效、行为变化或产生兼容问题。', 'Game updates may break the plugin or change its behavior.') }}</li>
          <li>{{ label('第三方 DLL 具有进程内原生代码权限；只应使用可信来源。', 'Third-party DLLs have native in-process access. Use only trusted sources.') }}</li>
        </ul>
        <p>
          {{ label(
            '插件按现状提供，不附带任何明示或暗示担保。作者、SSMT 集成者和分发者不对封号、数据损坏、设备故障、存档丢失或其他间接损失承担责任。',
            'The plugin is provided as-is without warranty. Its authors, SSMT integrators, and distributors are not responsible for account bans, data corruption, device failure, save loss, or indirect damage.',
          ) }}
        </p>
        <p class="maintenance-note">
          {{ label(
            '本 SSMT 适配版基于 Sp1cHless 的原项目修改。请勿到原项目作者处催更或反馈本适配版的问题。',
            'This SSMT adaptation is based on Sp1cHless\'s original project. Do not ask its author to expedite updates or report adaptation issues there.',
          ) }}
        </p>
      </div>
    </details>

    <div v-if="!snapshot && loading" class="settings-state">{{ label('正在读取配置…', 'Loading settings…') }}</div>
    <div v-else-if="error" class="settings-state is-error">{{ error }}</div>
    <template v-else-if="snapshot">
      <div v-if="!snapshot.ssmtEnabled" class="settings-notice">
        {{ label(
          '插件目前在 SSMT 中未启用。重新启用并启动游戏后才会注入。',
          'The plugin is disabled in SSMT. Enable it and restart the game to inject it.',
        ) }}
      </div>

      <div class="manager-layout">
        <aside class="manager-sidebar">
          <section class="side-card character-card">
            <div class="side-title">
              <span>{{ label('当前角色', 'Current character') }}</span>
              <span class="status-dot" :class="`is-${statusTone}`" />
            </div>
            <template v-if="snapshot.characterId">
              <strong class="character-name">{{ snapshot.characterDisplayName || snapshot.characterId }}</strong>
              <code class="character-id">{{ snapshot.characterId }}</code>
              <span v-if="!snapshot.characterInPreset" class="side-badge">
                {{ label('尚未写入当前预设', 'Not yet in the active preset') }}
              </span>
            </template>
            <p v-else class="side-empty">
              {{ label('进入游戏并切换角色后加载参数。', 'Enter the game and select a character to load parameters.') }}
            </p>
          </section>

          <section class="side-card">
            <div class="side-title">
              <span>{{ label('当前预设', 'Active preset') }}</span>
              <small>#{{ snapshot.revision }}</small>
            </div>
            <el-select
              v-model="activePreset"
              class="preset-select"
              :disabled="snapshot.presets.length === 0 || saving"
            >
              <el-option v-for="preset in snapshot.presets" :key="preset" :label="preset" :value="preset" />
            </el-select>
            <el-button
              class="preset-apply"
              type="primary"
              :loading="saving"
              :disabled="loading || !activePreset"
              @click="apply"
            >
              {{ label('应用预设', 'Apply preset') }}
            </el-button>
            <small class="side-hint">
              {{ label('应用后游戏内自动热加载。', 'The running game hot-reloads changes.') }}
            </small>
          </section>

          <nav class="editor-nav" :aria-label="label('参数分区', 'Parameter sections')">
            <a href="#basic-section">{{ label('基础与骨骼', 'Basic & bones') }}</a>
            <a href="#gait-section">{{ label('步态参数', 'Gait') }}</a>
            <a href="#advanced-section">{{ label('高级参数', 'Advanced') }}</a>
            <a href="#jump-section">{{ label('跳跃与惯性', 'Jump & inertia') }}</a>
          </nav>

          <section class="side-card runtime-card">
            <div class="side-title">
              <span>{{ label('运行时状态', 'Runtime status') }}</span>
              <span class="status-pill" :class="`is-${statusTone}`">{{ statusLabel }}</span>
            </div>
            <p v-if="statusDetails.length">{{ statusDetails.join(' · ') }}</p>
            <p v-else>{{ label('等待游戏报告角色与 Hook 状态。', 'Waiting for character and hook status.') }}</p>
            <p v-if="snapshot.status?.last_write_ms">
              {{ label('最近写入', 'Last write') }}: {{ snapshot.status.last_write_ms }} ms
            </p>
            <p v-if="snapshot.status?.error" class="runtime-error">{{ snapshot.status.error }}</p>
          </section>

          <details class="settings-paths">
            <summary>{{ label('安装位置', 'Installation paths') }}</summary>
            <dl>
              <dt>{{ label('数据目录', 'Data') }}</dt><dd>{{ snapshot.dataRoot }}</dd>
              <dt>{{ label('插件版本', 'Version') }}</dt><dd>{{ snapshot.pluginName }} v{{ snapshot.pluginVersion }}</dd>
            </dl>
          </details>
        </aside>

        <main class="manager-editor">
          <div class="editor-toolbar">
            <div>
              <span class="eyebrow">{{ label('角色参数', 'CHARACTER SETTINGS') }}</span>
              <h2>{{ snapshot.characterDisplayName || snapshot.characterId || label('等待当前角色', 'Waiting for character') }}</h2>
              <p>
                {{ snapshot.characterId
                  ? label('修改后保存到当前预设；应用预设会同步运行配置。', 'Save changes to the active preset; applying syncs the runtime config.')
                  : label('游戏报告当前角色后即可编辑。', 'Controls become available after the game reports a character.') }}
              </p>
            </div>
            <div class="toolbar-actions">
              <el-button :loading="loading" @click="load">{{ label('刷新', 'Refresh') }}</el-button>
              <el-button
                v-if="snapshot.characterId"
                :loading="characterSaving"
                @click="saveCharacter"
              >
                {{ label('保存角色', 'Save character') }}
              </el-button>
              <el-button
                type="primary"
                :loading="saving"
                :disabled="loading || !activePreset"
                @click="apply"
              >
                {{ label('应用设置', 'Apply settings') }}
              </el-button>
            </div>
          </div>

          <div v-if="editorDisabled" class="editor-empty">
            <strong>{{ label('尚未检测到当前角色', 'No active character detected') }}</strong>
            <p>{{ label('进入游戏并在角色界面切换一次角色，然后点击“刷新”。', 'Enter the game and switch characters once, then select Refresh.') }}</p>
          </div>
          <template v-else>
            <section id="basic-section" class="editor-card">
              <div class="section-heading">
                <div>
                  <h3>{{ label('基础与骨骼', 'Basic & bones') }}</h3>
                  <p>{{ label('角色运动模式、轴与骨骼绑定。', 'Motion mode, axis, and bone binding for this character.') }}</p>
                </div>
                <span>{{ label('角色专属', 'Character-specific') }}</span>
              </div>
              <div class="form-grid">
                <label v-for="field in generalFields" :key="field.path" class="form-field">
                  <span>{{ field.label }}</span>
                  <el-select
                    v-if="field.key === 'motion_mode'"
                    :model-value="String(field.value)"
                    :disabled="editorDisabled"
                    @change="(value: string) => setPathValue(field.path, value)"
                  >
                    <el-option :label="label('关闭', 'Off')" value="off" />
                    <el-option :label="label('合成运动', 'Synthetic')" value="synthetic" />
                    <el-option :label="label('原生增益', 'Amplify native')" value="amplify_native" />
                  </el-select>
                  <el-switch
                    v-else-if="typeof field.value === 'boolean'"
                    :model-value="field.value"
                    :disabled="editorDisabled"
                    @change="(value: boolean | string | number) => setPathValue(field.path, Boolean(value))"
                  />
                  <el-input-number
                    v-else-if="typeof field.value === 'number'"
                    :model-value="field.value"
                    :controls="false"
                    :step="0.01"
                    :disabled="editorDisabled"
                    @change="(value: number | undefined) => setPathValue(field.path, Number(value ?? 0))"
                  />
                  <el-input
                    v-else
                    :model-value="String(field.value)"
                    :disabled="editorDisabled"
                    @input="(value: string) => setPathValue(field.path, String(value))"
                  />
                </label>
                <label v-for="field in boneFields" :key="field.path" class="form-field">
                  <span>{{ field.label }}</span>
                  <el-switch
                    v-if="typeof field.value === 'boolean'"
                    :model-value="field.value"
                    :disabled="editorDisabled"
                    @change="(value: boolean | string | number) => setPathValue(field.path, Boolean(value))"
                  />
                  <el-input
                    v-else-if="typeof field.value === 'string'"
                    :model-value="field.value"
                    :disabled="editorDisabled"
                    @input="(value: string) => setPathValue(field.path, String(value))"
                  />
                  <el-input-number
                    v-else
                    :model-value="field.value"
                    :controls="false"
                    :step="0.01"
                    :disabled="editorDisabled"
                    @change="(value: number | undefined) => setPathValue(field.path, Number(value ?? 0))"
                  />
                </label>
              </div>
            </section>

            <section id="gait-section" class="editor-card">
              <div class="section-heading">
                <div>
                  <h3>{{ label('步态参数', 'Gait') }}</h3>
                  <p>{{ label('按动作调整上下摆幅、频率与相位。', 'Adjust up/down amplitude, frequency, and phase by locomotion state.') }}</p>
                </div>
                <span>{{ gaitRows.length }} {{ label('种动作', 'states') }}</span>
              </div>
              <div v-if="gaitColumns.length" class="gait-scroll">
                <table class="gait-table">
                  <thead>
                    <tr>
                      <th>{{ label('动作', 'State') }}</th>
                      <th v-for="column in gaitColumns" :key="column.key">{{ column.label }}</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr v-for="row in gaitRows" :key="row.key">
                      <th>{{ row.label }}</th>
                      <td v-for="column in gaitColumns" :key="column.key">
                        <el-switch
                          v-if="typeof row.values[column.key] === 'boolean'"
                          :model-value="Boolean(row.values[column.key])"
                          :disabled="editorDisabled"
                          @change="(value: boolean | string | number) => setPathValue(`gait.${row.key}.${column.key}`, Boolean(value))"
                        />
                        <el-input-number
                          v-else-if="typeof row.values[column.key] === 'number'"
                          :model-value="Number(row.values[column.key])"
                          :controls="false"
                          :step="0.01"
                          :disabled="editorDisabled"
                          @change="(value: number | undefined) => setPathValue(`gait.${row.key}.${column.key}`, Number(value ?? 0))"
                        />
                        <el-input
                          v-else
                          :model-value="String(row.values[column.key] ?? '')"
                          :disabled="editorDisabled"
                          @input="(value: string) => setPathValue(`gait.${row.key}.${column.key}`, String(value))"
                        />
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>
              <div v-else class="inline-empty">{{ label('当前角色没有步态参数。', 'No gait parameters are available.') }}</div>
            </section>

            <div class="editor-columns">
              <section id="advanced-section" class="editor-card">
                <div class="section-heading">
                  <div>
                    <h3>{{ label('高级参数', 'Advanced') }}</h3>
                    <p>{{ label('通常无需修改。', 'Usually do not touch.') }}</p>
                  </div>
                </div>
                <div class="compact-list">
                  <label v-for="field in axisFields" :key="field.path" class="compact-field">
                    <span>
                      <strong>{{ field.label }}</strong>
                      <small>{{ field.path }}</small>
                    </span>
                    <el-select
                      v-if="field.key === 'name'"
                      :model-value="String(field.value)"
                      :disabled="editorDisabled"
                      @change="(value: string) => setPathValue(field.path, value)"
                    >
                      <el-option :label="label('自动', 'Auto')" value="auto" />
                      <el-option label="X" value="X" />
                      <el-option label="Y" value="Y" />
                      <el-option label="Z" value="Z" />
                    </el-select>
                    <el-select
                      v-else-if="field.key === 'sign'"
                      :model-value="Number(field.value)"
                      :disabled="editorDisabled"
                      @change="(value: number) => setPathValue(field.path, Number(value))"
                    >
                      <el-option :label="label('正方向', 'Positive')" :value="1" />
                      <el-option :label="label('负方向', 'Negative')" :value="-1" />
                    </el-select>
                    <el-input-number
                      v-else-if="typeof field.value === 'number'"
                      :model-value="field.value"
                      :controls="false"
                      :step="0.01"
                      :disabled="editorDisabled"
                      @change="(value: number | undefined) => setPathValue(field.path, Number(value ?? 0))"
                    />
                    <el-input
                      v-else
                      :model-value="String(field.value)"
                      :disabled="editorDisabled"
                      @input="(value: string) => setPathValue(field.path, String(value))"
                    />
                  </label>
                  <label v-for="field in envelopeFields" :key="field.path" class="compact-field">
                    <span>
                      <strong>{{ field.label }}</strong>
                      <small>{{ field.hint || field.path }}</small>
                    </span>
                    <el-input-number
                      v-if="typeof field.value === 'number'"
                      :model-value="field.value"
                      :controls="false"
                      :step="0.01"
                      :disabled="editorDisabled"
                      @change="(value: number | undefined) => setPathValue(field.path, Number(value ?? 0))"
                    />
                    <el-input
                      v-else
                      :model-value="String(field.value)"
                      :disabled="editorDisabled"
                      @input="(value: string) => setPathValue(field.path, String(value))"
                    />
                  </label>
                  <label v-for="field in nativeFields" :key="field.path" class="compact-field">
                    <span>
                      <strong>{{ field.label }}</strong>
                      <small>{{ field.path }}</small>
                    </span>
                    <el-input-number
                      v-if="typeof field.value === 'number'"
                      :model-value="field.value"
                      :controls="false"
                      :step="0.01"
                      :disabled="editorDisabled"
                      @change="(value: number | undefined) => setPathValue(field.path, Number(value ?? 0))"
                    />
                    <el-input
                      v-else
                      :model-value="String(field.value)"
                      :disabled="editorDisabled"
                      @input="(value: string) => setPathValue(field.path, String(value))"
                    />
                  </label>
                </div>
              </section>

              <section id="jump-section" class="editor-card">
                <div class="section-heading">
                  <div>
                    <h3>{{ label('跳跃与惯性', 'Jump & inertia') }}</h3>
                    <p>{{ label('控制起跳、下落和落地时的身体惯性。', 'Controls body inertia during takeoff, falling, and landing.') }}</p>
                  </div>
                  <el-switch
                    v-if="characterDraft?.jump"
                    :model-value="Boolean(characterDraft.jump.enabled)"
                    :disabled="editorDisabled"
                    @change="(value: boolean | string | number) => setPathValue('jump.enabled', Boolean(value))"
                  />
                </div>
                <div class="jump-grid">
                  <label v-for="field in jumpFields.filter(field => field.key !== 'enabled')" :key="field.path" class="compact-field">
                    <span>
                      <strong>{{ field.label }}</strong>
                      <small>{{ field.hint || field.path }}</small>
                    </span>
                    <el-switch
                      v-if="typeof field.value === 'boolean'"
                      :model-value="field.value"
                      :disabled="editorDisabled"
                      @change="(value: boolean | string | number) => setPathValue(field.path, Boolean(value))"
                    />
                    <el-select
                      v-else-if="field.key === 'mode'"
                      :model-value="String(field.value)"
                      :disabled="editorDisabled"
                      @change="(value: string) => setPathValue(field.path, value)"
                    >
                      <el-option :label="label('关闭', 'Off')" value="off" />
                      <el-option :label="label('落地阻尼', 'Landing damped')" value="landing_damped" />
                    </el-select>
                    <el-input-number
                      v-else-if="typeof field.value === 'number'"
                      :model-value="field.value"
                      :controls="false"
                      :step="0.01"
                      :disabled="editorDisabled"
                      @change="(value: number | undefined) => setPathValue(field.path, Number(value ?? 0))"
                    />
                    <el-input
                      v-else
                      :model-value="String(field.value)"
                      :disabled="editorDisabled"
                      @input="(value: string) => setPathValue(field.path, String(value))"
                    />
                  </label>
                </div>
              </section>
            </div>

            <section v-for="group in otherGroups" :key="group.key" class="editor-card">
              <div class="section-heading">
                <div>
                  <h3>{{ group.label }}</h3>
                  <p>{{ group.key }}</p>
                </div>
              </div>
              <div class="compact-list">
                <label v-for="field in group.fields" :key="field.path" class="compact-field">
                  <span>
                    <strong>{{ field.label }}</strong>
                    <small>{{ field.path }}</small>
                  </span>
                  <el-switch
                    v-if="typeof field.value === 'boolean'"
                    :model-value="field.value"
                    :disabled="editorDisabled"
                    @change="(value: boolean | string | number) => setPathValue(field.path, Boolean(value))"
                  />
                  <el-input-number
                    v-else-if="typeof field.value === 'number'"
                    :model-value="field.value"
                    :controls="false"
                    :step="0.01"
                    :disabled="editorDisabled"
                    @change="(value: number | undefined) => setPathValue(field.path, Number(value ?? 0))"
                  />
                  <el-input
                    v-else
                    :model-value="String(field.value)"
                    :disabled="editorDisabled"
                    @input="(value: string) => setPathValue(field.path, String(value))"
                  />
                </label>
              </div>
            </section>
          </template>
        </main>
      </div>
    </template>
  </section>
</template>

<style scoped>
.secondary-motion-settings {
  --el-fill-color-blank: #0f171b;
  --el-fill-color-light: #162127;
  --el-fill-color-lighter: #1b282f;
  --el-font-size-base: 12px;
  --el-border-color: rgba(142, 166, 174, .20);
  --el-border-color-light: rgba(142, 166, 174, .14);
  --el-text-color-primary: #f0f5f6;
  --el-text-color-regular: #d0dbdf;
  --el-text-color-secondary: #b3c1c6;
  --el-text-color-placeholder: #83979d;
  --t-page-text: #f0f5f6;
  --t-page-text-muted: #b8c6cb;
  min-width: 0;
  padding-bottom: 42px;
  color: var(--t-page-text);
  font-size: 12px;
  line-height: 1.5;
}
.eyebrow {
  display: block;
  margin-bottom: 5px;
  color: var(--ssmt-brand-accent, #d8e94e);
  font: 700 10px/1.2 ui-monospace, Consolas, monospace;
  letter-spacing: .12em;
}
.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 24px;
  padding: 18px 20px;
  border: 1px solid rgba(var(--ssmt-brand-accent-rgb, 216, 233, 78), .20);
  border-radius: 10px;
  background:
    radial-gradient(circle at 100% 0, rgba(var(--ssmt-brand-accent-rgb, 216, 233, 78), .10), transparent 38%),
    linear-gradient(135deg, rgba(22, 35, 40, .98), rgba(11, 18, 22, .98));
}
.page-header h1 {
  margin: 0 0 5px;
  font-size: 20px;
  line-height: 1.25;
}
.page-header p {
  max-width: 720px;
  margin: 0;
  color: var(--t-page-text-muted);
  font-size: 12px;
  line-height: 1.65;
}
.global-switch {
  display: flex;
  flex: none;
  align-items: center;
  gap: 18px;
  padding: 10px 12px;
  border: 1px solid rgba(142, 166, 174, .16);
  border-radius: 8px;
  background: rgba(4, 10, 13, .46);
}
.global-switch span {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.global-switch strong { font-size: 13px; }
.global-switch small { color: var(--t-page-text-muted); font-size: 11px; }
.risk-notice {
  margin-top: 10px;
  border: 1px solid rgba(255, 170, 70, .24);
  border-radius: 8px;
  background: rgba(86, 50, 14, .16);
  color: rgba(255, 220, 170, .92);
}
.risk-notice summary {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 9px 12px;
  cursor: pointer;
  list-style: none;
  color: #ffd08a;
  font-size: 12px;
}
.risk-notice summary::-webkit-details-marker { display: none; }
.risk-notice summary small {
  color: rgba(255, 220, 170, .70);
  font-size: 10px;
  font-weight: 400;
}
.risk-body {
  padding: 0 13px 11px;
  border-top: 1px solid rgba(255, 170, 70, .12);
  font-size: 11px;
  line-height: 1.65;
}
.risk-body p { margin: 9px 0 0; }
.risk-body ul { margin: 8px 0 0; padding-left: 18px; }
.risk-body li + li { margin-top: 2px; }
.maintenance-note {
  padding: 8px 10px;
  border-left: 2px solid rgba(255, 208, 138, .66);
  border-radius: 3px;
  background: rgba(255, 208, 138, .06);
}
.settings-state,
.settings-notice {
  margin-top: 12px;
  padding: 12px 14px;
  border-radius: 8px;
  background: var(--t-surface-subtle);
  color: var(--t-page-text-muted);
  font-size: 12px;
  line-height: 1.6;
}
.settings-state.is-error { color: var(--t-danger-text); background: var(--t-danger-bg); }
.settings-notice {
  margin-bottom: 10px;
  color: var(--t-warning-text);
  background: var(--t-warning-bg);
  border: 1px solid var(--t-warning-border);
}
.manager-layout {
  display: grid;
  grid-template-columns: 220px minmax(0, 1fr);
  align-items: start;
  gap: 14px;
  margin-top: 14px;
}
.manager-sidebar {
  position: sticky;
  top: 8px;
  display: grid;
  gap: 9px;
  min-width: 0;
}
.side-card,
.editor-card {
  border: 1px solid var(--market-line, rgba(142, 166, 174, .16));
  border-radius: 9px;
  background: var(--t-surface-subtle);
}
.side-card { padding: 12px; }
.side-title {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 9px;
  color: var(--t-page-text-muted);
  font-size: 11px;
}
.side-title small { font: 10px/1.3 ui-monospace, Consolas, monospace; opacity: .88; }
.status-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: #71858b;
  box-shadow: 0 0 0 3px rgba(113, 133, 139, .12);
}
.status-dot.is-ready { background: #91e0b1; box-shadow: 0 0 0 3px rgba(145, 224, 177, .12); }
.status-dot.is-warning { background: #ffd08a; box-shadow: 0 0 0 3px rgba(255, 208, 138, .12); }
.status-dot.is-danger { background: #efaaa4; box-shadow: 0 0 0 3px rgba(239, 170, 164, .12); }
.character-name {
  display: block;
  overflow: hidden;
  font-size: 16px;
  line-height: 1.35;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.character-id {
  display: block;
  margin-top: 3px;
  overflow: hidden;
  color: var(--t-page-text-muted);
  font-size: 11px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.side-badge {
  display: inline-block;
  margin-top: 8px;
  padding: 3px 6px;
  border-radius: 4px;
  background: var(--t-warning-bg);
  color: var(--t-warning-text);
  font-size: 10px;
}
.side-empty {
  margin: 0;
  color: var(--t-page-text-muted);
  font-size: 11px;
  line-height: 1.6;
}
.preset-select { width: 100%; }
.preset-apply {
  width: 100%;
  margin-top: 8px;
}
.side-hint {
  display: block;
  margin-top: 7px;
  color: var(--t-page-text-muted);
  font-size: 11px;
  line-height: 1.55;
}
.editor-nav {
  display: grid;
  gap: 2px;
  padding: 5px;
  border: 1px solid var(--market-line, rgba(142, 166, 174, .12));
  border-radius: 8px;
  background: rgba(5, 11, 14, .44);
}
.editor-nav a {
  padding: 7px 9px;
  border-radius: 5px;
  color: var(--t-page-text-muted);
  font-size: 11px;
  text-decoration: none;
  transition: background .15s ease, color .15s ease;
}
.editor-nav a:hover {
  background: rgba(var(--ssmt-brand-accent-rgb, 216, 233, 78), .08);
  color: var(--t-page-text);
}
.runtime-card p {
  margin: 0;
  color: var(--t-page-text-muted);
  font-size: 11px;
  line-height: 1.65;
}
.runtime-card p + p { margin-top: 5px; }
.runtime-card .runtime-error {
  margin-top: 7px;
  color: var(--t-danger-text);
  font-family: ui-monospace, Consolas, monospace;
  word-break: break-word;
}
.status-pill {
  flex: none;
  padding: 2px 6px;
  border: 1px solid currentColor;
  border-radius: 999px;
  font-size: 10px;
}
.status-pill.is-ready { color: var(--t-success-text); background: var(--t-success-bg); }
.status-pill.is-warning { color: var(--t-warning-text); background: var(--t-warning-bg); }
.status-pill.is-danger { color: var(--t-danger-text); background: var(--t-danger-bg); }
.status-pill.is-idle { color: var(--t-page-text-muted); }
.settings-paths {
  padding: 9px 11px;
  color: var(--t-page-text-muted);
  font-size: 11px;
}
.settings-paths summary { cursor: pointer; }
.settings-paths dl { display: grid; gap: 5px; margin: 8px 0 0; }
.settings-paths dt { color: var(--t-page-text-muted); opacity: .92; }
.settings-paths dd {
  margin: 1px 0 0;
  color: var(--t-page-text);
  overflow-wrap: anywhere;
  user-select: text;
}
.manager-editor { min-width: 0; }
.editor-toolbar {
  position: sticky;
  z-index: 4;
  top: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  margin-bottom: 10px;
  padding: 13px 15px;
  border: 1px solid var(--market-line, rgba(142, 166, 174, .16));
  border-radius: 9px;
  background: rgba(10, 17, 21, .96);
  box-shadow: 0 8px 22px rgba(0, 0, 0, .18);
  backdrop-filter: blur(10px);
}
.editor-toolbar h2 {
  margin: 0;
  font-size: 17px;
}
.editor-toolbar p {
  margin: 3px 0 0;
  color: var(--t-page-text-muted);
  font-size: 11px;
  line-height: 1.5;
}
.toolbar-actions {
  display: flex;
  flex: none;
  flex-wrap: wrap;
  justify-content: flex-end;
  gap: 7px;
}
.toolbar-actions :deep(.el-button + .el-button) { margin-left: 0; }
.editor-empty {
  padding: 28px 24px;
  border: 1px dashed rgba(142, 166, 174, .24);
  border-radius: 9px;
  background: rgba(14, 23, 27, .58);
  text-align: center;
}
.editor-empty strong { font-size: 14px; }
.editor-empty p {
  margin: 6px 0 0;
  color: var(--t-page-text-muted);
  font-size: 11px;
}
.editor-card {
  scroll-margin-top: 92px;
  padding: 14px;
}
.editor-card + .editor-card,
.editor-columns + .editor-card { margin-top: 10px; }
.section-heading {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 11px;
}
.section-heading h3 {
  margin: 0;
  color: #e8f1d2;
  font-size: 14px;
}
.section-heading p {
  margin: 3px 0 0;
  color: var(--t-page-text-muted);
  font-size: 11px;
  line-height: 1.55;
}
.section-heading > span {
  flex: none;
  color: var(--t-page-text-muted);
  font-size: 11px;
}
.form-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(230px, 1fr));
  gap: 8px;
}
.form-field {
  display: grid;
  grid-template-columns: minmax(110px, .8fr) minmax(130px, 1.2fr);
  align-items: center;
  gap: 12px;
  min-height: 36px;
  padding: 6px 8px;
  border: 1px solid rgba(142, 166, 174, .09);
  border-radius: 6px;
  background: rgba(0, 0, 0, .10);
  color: var(--t-page-text);
  font-size: 11px;
}
.form-field > span { color: #dce5e8; font-weight: 500; }
.form-field > .el-switch { justify-self: start; }
.gait-scroll {
  overflow-x: auto;
  border: 1px solid rgba(142, 166, 174, .10);
  border-radius: 6px;
}
.gait-table {
  width: 100%;
  min-width: 760px;
  border-collapse: collapse;
  table-layout: fixed;
}
.gait-table th,
.gait-table td {
  padding: 5px 6px;
  border-right: 1px solid rgba(142, 166, 174, .08);
  border-bottom: 1px solid rgba(142, 166, 174, .08);
  text-align: center;
}
.gait-table tr:last-child th,
.gait-table tr:last-child td { border-bottom: 0; }
.gait-table th:last-child,
.gait-table td:last-child { border-right: 0; }
.gait-table thead th {
  height: 28px;
  background: rgba(142, 166, 174, .06);
  color: var(--t-page-text-muted);
  font-size: 11px;
  font-weight: 500;
}
.gait-table thead th:first-child,
.gait-table tbody th {
  width: 78px;
  text-align: left;
}
.gait-table tbody th {
  padding-left: 9px;
  background: rgba(0, 0, 0, .10);
  color: var(--t-page-text);
  font-size: 11px;
  font-weight: 600;
}
.gait-table :deep(.el-input-number),
.gait-table :deep(.el-input) { width: 100%; }
.gait-table :deep(.el-input__wrapper) { padding-right: 6px; padding-left: 6px; }
.gait-table :deep(.el-input__inner) { text-align: right; }
.editor-columns {
  display: grid;
  grid-template-columns: minmax(300px, .9fr) minmax(360px, 1.1fr);
  align-items: start;
  gap: 10px;
  margin-top: 10px;
}
.compact-list,
.jump-grid { display: grid; gap: 6px; }
.compact-field {
  display: grid;
  grid-template-columns: minmax(140px, 1fr) minmax(92px, 150px);
  align-items: center;
  gap: 10px;
  min-height: 38px;
  padding: 5px 7px;
  border-bottom: 1px solid rgba(142, 166, 174, .08);
}
.compact-field:last-child { border-bottom: 0; }
.compact-field > span {
  display: flex;
  min-width: 0;
  flex-direction: column;
  gap: 2px;
}
.compact-field strong {
  color: var(--t-page-text);
  font-size: 11px;
  font-weight: 500;
}
.compact-field small {
  overflow: hidden;
  color: var(--t-page-text-muted);
  font-size: 10px;
  line-height: 1.45;
  text-overflow: ellipsis;
}
.compact-field :deep(.el-input-number),
.compact-field :deep(.el-input),
.compact-field :deep(.el-select) { width: 100%; }
.inline-empty {
  padding: 12px;
  border-radius: 6px;
  background: rgba(0, 0, 0, .10);
  color: var(--t-page-text-muted);
  font-size: 11px;
}
.secondary-motion-settings :deep(.el-input__inner),
.secondary-motion-settings :deep(.el-select__selected-item) {
  font-size: 12px;
}
@media (max-width: 980px) {
  .manager-layout { grid-template-columns: minmax(0, 1fr); }
  .manager-sidebar {
    position: static;
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
  .editor-nav,
  .settings-paths { grid-column: 1 / -1; }
  .editor-nav {
    grid-template-columns: repeat(4, minmax(0, 1fr));
  }
  .editor-nav a { text-align: center; }
  .editor-columns { grid-template-columns: minmax(0, 1fr); }
}
@media (max-width: 680px) {
  .page-header,
  .editor-toolbar {
    align-items: stretch;
    flex-direction: column;
  }
  .global-switch { justify-content: space-between; }
  .manager-sidebar { grid-template-columns: minmax(0, 1fr); }
  .editor-nav { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .toolbar-actions { justify-content: flex-start; }
  .form-field { grid-template-columns: minmax(0, 1fr); gap: 5px; }
  .compact-field { grid-template-columns: minmax(0, 1fr); gap: 5px; }
}
</style>
