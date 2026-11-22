package dev.ultrascale.mobile;

interface ICommandService {
    String readLayerTelemetry(String targetPackage);
    String configureLayer(String targetPackage, String layerPackage, String api, boolean enable);
}
