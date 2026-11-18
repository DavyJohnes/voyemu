import android.net.LocalSocket;

import com.qinggan.canbus.VehicleState;
import com.qinggan.canbus.service.CanModelBridge;
import com.qinggan.canbus.service.CanModelClient;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BooleanSupplier;

/** Host model/Java integration; native kernel/Binder delivery is accepted separately in the VM. */
public final class Api30NativeModelBridgeTest {
    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static void await(BooleanSupplier condition, String message) throws Exception {
        long end = System.nanoTime() + 3000000000L;

        while (!condition.getAsBoolean() && System.nanoTime() < end) {
            Thread.sleep(10);
        }

        need(condition.getAsBoolean(), message);
    }

    static long pair(int high, int low) {
        return ((long) high << 32) | (low & 0xffffffffL);
    }

    public static void main(String[] args) throws Exception {
        CanModelBridge bridge = new CanModelBridge();
        AtomicInteger feedback = new AtomicInteger(),
                responses = new AtomicInteger(),
                lastResponseValue = new AtomicInteger(-1);

        bridge.start(
                new CanModelBridge.Listener() {
                    public void onModelState(
                            CanModelBridge source,
                            VehicleState state,
                            int value,
                            long session,
                            long epoch,
                            long revision) {
                        need(source.isCurrent(session, epoch, revision), "stale callback");
                        feedback.incrementAndGet();
                    }

                    public void onModelResponse(
                            CanModelBridge source,
                            VehicleState state,
                            int value,
                            long session,
                            long epoch) {
                        need(source.isCurrentEpoch(session, epoch), "stale response");
                        lastResponseValue.set(value);
                        responses.incrementAndGet();
                    }

                    public void onModelAcc(CanModelBridge source, int value) {
                        need(source.getAccStatus() == value, "ACC delivery differs from getter");
                    }
                });

        try {
            await(
                    () -> bridge.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW) == 2,
                    "initial model snapshot unavailable");
            byte[] warning = {0x6a, 8, 0, 0, 0, 0, 0, 0x10, 0x7c, 0};
            LocalSocket.nativeWrite(warning);
            await(
                    () -> bridge.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW) == 1,
                    "native PW OFF not reflected in OEM model getter");
            warning[7] = 0x20;
            LocalSocket.nativeWrite(warning);
            await(
                    () -> bridge.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW) == 2,
                    "native PW ON not reflected");
            byte[] forced = {0x68, 8, 0, 0, 0, (byte) 0xf0, 0x2c, 0x50, 0, 0};
            LocalSocket.nativeWrite(forced);
            await(
                    () -> bridge.getVehicleState(VehicleState.IVI_SOC_MODESET) == 5,
                    "native FEV ON not reflected");
            forced[7] = 0x20;
            LocalSocket.nativeWrite(forced);
            await(
                    () -> bridge.getVehicleState(VehicleState.IVI_SOC_MODESET) == 2,
                    "native FEV OFF not reflected");
            bridge.setVehicleState(VehicleState.HUM_VSP_FUNCTION_SW, 1);
            await(
                    () -> bridge.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW) == 1,
                    "OEM setter did not mutate shared model");
            await(() -> responses.get() > 0, "OEM setting response missing");
            CanModelClient client = new CanModelClient();
            int[] before = client.exchange(1, 0, 0, 0, 0);
            long session = pair(before[6], before[7]), epoch = pair(before[8], before[9]);
            int[] stale = client.exchange(2, 665, 2, session, epoch + 1);
            need(stale[1] == -2, "stale epoch accepted");
            need(client.exchange(1, 0, 0, 0, 0)[2] == 1, "stale command changed state");
            client.close();

            for (int mode = 1; mode <= 5; mode++) {
                final int expected = mode;
                bridge.setVehicleState(VehicleState.IVI_SOC_MODESET, mode);
                await(
                        () -> bridge.getVehicleState(VehicleState.IVI_SOC_MODESET) == expected,
                        "OEM energy-mode " + mode + " rejected by shared model");
            }

            await(
                    () -> lastResponseValue.get() == 5,
                    "OEM successful FEV Ex callback must carry raw5");
            int beforeReject = responses.get();
            bridge.setVehicleState(VehicleState.IVI_SOC_MODESET, 6);
            await(() -> responses.get() > beforeReject, "rejected model setting response missing");
            need(
                    lastResponseValue.get() == 5,
                    "OEM rejected setting Ex callback must carry current getter5");
            need(
                    bridge.getVehicleState(VehicleState.IVI_SOC_MODESET) == 5,
                    "rejected setting changed state");
            CanModelClient admission = new CanModelClient();
            int[] current = admission.exchange(1, 0, 0, 0, 0);
            int[] rejected =
                    admission.exchange(
                            2, 957, 6, pair(current[6], current[7]), pair(current[8], current[9]));
            need(
                    rejected[1] == -1,
                    "native admission failure must remain separate from Ex raw getter5");
            admission.close();
            int beforeOff = responses.get();
            bridge.setVehicleState(VehicleState.IVI_SOC_MODESET, 2);
            await(
                    () ->
                            responses.get() > beforeOff
                                    && lastResponseValue.get() == 2
                                    && bridge.getVehicleState(VehicleState.IVI_SOC_MODESET) == 2,
                    "OEM FEV OFF Ex raw2 and getter must settle independently");
            need(
                    bridge.getVehicleState(VehicleState.IVI_SOC_MODESET) == 2,
                    "FEV OFF getter did not settle");
            int beforeOffReject = responses.get();
            bridge.setVehicleState(VehicleState.IVI_SOC_MODESET, 6);
            await(
                    () -> responses.get() > beforeOffReject,
                    "rejected setting response after OFF missing");
            need(
                    lastResponseValue.get() == 2,
                    "OEM rejected setting must echo current getter2, not status0");
            need(
                    bridge.getVehicleState(VehicleState.IVI_SOC_MODESET) == 2,
                    "rejected setting changed OFF state");
            bridge.setAccStatus(0);

            await(
                    () -> bridge.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW) == -1,
                    "ACC OFF must invalidate getters");
            await(() -> bridge.getAccStatus() == 0, "model ACC OFF not reported by getAccStatus");
            bridge.setAccStatus(2);
            await(
                    () -> bridge.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW) == 2,
                    "ACC ON must reset via model fixture");
            need(bridge.getAccStatus() == 2, "model ACC ON not reported by getAccStatus");
            LocalSocket.loseSource();
            await(
                    () -> bridge.getVehicleState(VehicleState.HUM_VSP_FUNCTION_SW) == -1,
                    "lease EOF must invalidate getter");
            need(bridge.getAccStatus() == -1, "lease EOF must leave ACC unknown");
            need(feedback.get() >= 6, "model feedback incomplete");
            System.out.println(
                    "API30 production C model ↔ Java bridge host PASS: raw PW/FEV, Ex5/2/readback"
                            + " fallback, admission, epoch, ACC reset, lease loss");
        } finally {
            bridge.stop();
            LocalSocket.finish();
        }
    }
}
