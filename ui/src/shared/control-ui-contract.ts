// Control UI bootstrap configuration
export const CONTROL_UI_BOOTSTRAP_CONFIG_PATH = "/.well-known/ravbot-control-ui-bootstrap.json";

export interface ControlUIBootstrapConfig {
  gatewayUrl?: string;
  authToken?: string;
  deviceIdentity?: string;
}
