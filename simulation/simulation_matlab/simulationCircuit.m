l = 'simulationElectronique';
load_system(model);
electronicSimOut = sim(model);

simTime = electronicSimOut.tout;   % or wherever your time vector is

irradianceVals = zeros(length(simTime), 1);
powerVals = zeros(length(simTime), 1);

figure;
h = animatedline('Color', 'b', 'LineWidth', 2);
xlabel('Time (s)');
ylabel('Power (W)');
title('Solar Cell Power Output');
grid on;

for k = 1:length(simTime)
    sunAngle = sunAngleData(k);
    irradiance = max(0, 1361 * cosd(sunAngle));
    irradianceVals(k) = irradiance;

    % sensor outputs at time index k
    voltage = electronicSimOut.voltage.signals.values(k);
    current = electronicSimOut.current.signals.values(k);

    powerVals(k) = voltage * current;

    addpoints(h, simTime(k), powerVals(k));
    drawnow;

    fprintf('t=%.1f s | Voltage=%.2f V | Current=%f A\n', ...
        simTime(k), voltage, current);

end

fprintf('Simulation replay finished.\n');
