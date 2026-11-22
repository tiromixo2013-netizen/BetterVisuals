package dev.ultrascale.mobile;

interface ICommandService {
    String readLayerTelemetry(String targetPackage);
    String configureLayer(String targetPackage, String layerPackage, String api, boolean enable);
    String readAsphalt8Config();
    String deployAsphalt8Config(String json);
    String restoreAsphalt8Config();
}
