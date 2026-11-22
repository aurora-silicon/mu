# SPDX-License-Identifier: BSD-2-Clause-Patent
APRIORI DXE {
  INF AppleSiliconPkg/Override/Generated/MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  # First driver after the PCD service, which its DxePcdLib may look up.
  INF T8152FamilyPkg/Drivers/J804CheckpointDxe/J804CheckpointDxe.inf
  INF AppleSiliconPkg/Override/ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF AppleSiliconPkg/Drivers/AppleAicDxe/AppleAicDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF AppleSiliconPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
}
