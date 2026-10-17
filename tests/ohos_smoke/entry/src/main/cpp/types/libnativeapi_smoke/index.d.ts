import { common, Want } from '@kit.AbilityKit';
import { image } from '@kit.ImageKit';
import { window, NodeContent } from '@kit.ArkUI';
export const run: () => Promise<string>;
export const attachWindowStage: (stage: window.WindowStage) => boolean;

export const attachContext: (context: common.UIAbilityContext) => boolean;

export const runPickers: (cancelOnly?: boolean) => Promise<string>;
export const runUrls: (link: string, useC: boolean) => Promise<string>;
export const runNotifications: (mode?: string) => Promise<string>;
export const handleNotificationWant: (want: Want, onCreate?: boolean) => boolean;

export const attachViewContent: (owner: window.Window, content: NodeContent) => boolean;
export const detachViewContent: () => void;
export const runViews: () => void;

export const attachMenuHost: (owner: window.Window, host: Object) => boolean;
export const runMenus: () => Promise<string>;

export const runAccessibility: (expected: boolean, enabledCount: number, runId: string) => Promise<string>;

export const applicationLifecycle: (context: common.UIAbilityContext, event: string) => boolean;
export const runApplication: () => string;
export const setApplicationTheme: (mode: number, useC: boolean, worker: boolean) => Promise<boolean>;
export const runApplicationQuit: (mode: number) => boolean;

export const runTray: (command: string) => string;
export const runTrayWorker: () => Promise<boolean>;

export const setupIntegrations: () => string;
export const integrationState: (check?: string) => string;
export const integrationAction: (command: string, useC: boolean, worker: boolean) => Promise<string>;

export const occlusionCommand: (command: string) => string;
export const occlusionCheck: (expected: number, worker: boolean, shown: boolean) => Promise<string>;

export const styleState: (command?: string) => string;
export const styleAction: (command: string, useC: boolean, worker: boolean) => Promise<string>;

export const shapeState: (command?: string) => string;
export const shapeAction: (command: string, useC: boolean, worker: boolean) => Promise<string>;

export const dragState: (command?: string) => string;
export const dragStart: (main: boolean, useC: boolean) => string;

export const controlsState: (command?: string) => string;
export const aspectState: (command?: string) => string;
export const aspectAction: (command: string, useC: boolean, worker: boolean) => Promise<string>;
export const controlsAction: (command: string, useC: boolean, worker: boolean) => Promise<string>;

export const cursorProbe: (phase: string, eventX: number, eventY: number) => string;

export const displayState: (command?: string, request?: string) => string;

export const clipboardInterop: (command: string, request: string) => void;

export const shortcutState: (command: string, request: string) => string;

export const dropState: (command: string, request: string) => string;

export const sourceState: (command: string, request: string) => string;
export const sourceBegin: () => boolean;

export const nativeCloseState: (command: string, request: string) => string;
export const nativeCloseAttach: (target: object) => string;

export const shortcutHostAttach: (target: window.Window) => number;
export const shortcutHostDetach: (token: number) => void;
export const shortcutKey: (token: number, code: number, type: number, modifiers: number, device: number) => boolean;

export const startupCheck: (supported: boolean, enabled: boolean) => Promise<string>;

export const keyboardState: (command: string, request: string) => string;

export const deviceInfoCheck: () => Promise<string>;

export const preferencesCommand: (command: string, scope: string) => Promise<string>;

export const managerState: (command: string) => string;
export const managerQuery: (x: number, y: number) => Promise<string>;

export const parentState: (command: string) => string;
export const parentCommand: (action: string, receiver: number, target: number, useC: boolean, worker: boolean) => Promise<string>;

export const tooltipAttach: (window: Object, content: Object, host: Object) => boolean;
export const tooltipCommand: (action: string) => string;

export const foundationValues: (worker: boolean) => Promise<string>;
export const foundationWindow: (action: string) => string;

export const shadowProbeImage: (width: number, height: number, density: number, blur: number, x: number, y: number) => { pixels: image.PixelMap; margin: number; width: number; height: number; };

export const attachShadowHost: (host: Object) => boolean;
export const shadowTargetAttach: (target: window.Window, primary?: boolean) => string;
export const shadowState: (action: string) => string;
export const shadowAction: (action: string, useC: boolean, worker: boolean) => Promise<boolean>;

export const titleState: (command?: string, child?: Object) => string;
export const titleAction: (target: number, useC: boolean, worker: boolean, expected: boolean, overlap: boolean) => Promise<string>;

export const dragSessionProbe: () => string;

export const fullscreenState: (command?: string, child?: Object) => string;
export const fullscreenAction: (target: number, enter: boolean, useC: boolean, worker: boolean) => Promise<string>;

export const modeState: (command?: string, child?: Object) => string;
export const modeSeed: (target: number, modes: number[]) => boolean;
export const modeAction: (target: number, enabled: boolean, useC: boolean, worker: boolean, batch: boolean, operation: string) => Promise<string>;

export const layoutState: (command?: string, child?: Object) => string;
export const layoutAction: (target: number, enabled: boolean, useC: boolean, worker: boolean) => Promise<boolean>;

export const clipboardBoundary: (command: string, request: string) => void;
