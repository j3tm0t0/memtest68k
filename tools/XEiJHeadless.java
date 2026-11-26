/* Emulator-side observer. Never linked into the distributed target images. */
import java.awt.Graphics2D;
import java.awt.image.BufferedImage;
import java.lang.instrument.Instrumentation;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.Properties;
import java.util.TimerTask;
import javax.imageio.ImageIO;
import javax.swing.SwingUtilities;
import xeij.CONDevice;
import xeij.InstructionBreakPoint;
import xeij.Keyboard;
import xeij.MainMemory;
import xeij.XEiJ;

public class XEiJHeadless {
    static final String[] FIELDS = {
        "magic", "version", "cpu", "ram", "low_start", "low_bytes", "tested_bytes",
        "passes", "errors", "low_runs", "low_errors", "probe_status", "old_vbr",
        "active_vbr", "restored_vbr", "restore_errors", "phase", "completed_mask", "first_address"
    };
    static int word(int address) {
        byte[] m = MainMemory.mmrM8;
        return (m[address] & 255) << 24 | (m[address+1] & 255) << 16 |
               (m[address+2] & 255) << 8 | m[address+3] & 255;
    }
    static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    static void key(String name) throws Exception {
        int code = (Integer)((Map<?,?>)CONDevice.conKeyMap).get(name);
        Keyboard.kbdCommandPress(code);
        Thread.sleep(120);
        Keyboard.kbdCommandRelease(code);
        Thread.sleep(300);
    }
    static void screen(Path path) throws Exception {
        SwingUtilities.invokeAndWait(() -> {
            try {
                var panel = XEiJ.pnlPanel;
                BufferedImage image = new BufferedImage(panel.getWidth(), panel.getHeight(), BufferedImage.TYPE_INT_RGB);
                Graphics2D graphics = image.createGraphics();
                panel.paint(graphics);
                graphics.dispose();
                ImageIO.write(image, "png", path.toFile());
            } catch (Exception e) { throw new RuntimeException(e); }
        });
    }
    public static void premain(String argument, Instrumentation instrumentation) {
        Thread thread = new Thread(() -> {
            Path output = null;
            int report = 0;
            boolean observed = false, injected = false;
            String failure = null;
            try {
                Properties p = new Properties();
                try (var input = Files.newInputStream(Path.of(argument))) { p.load(input); }
                output = Path.of(p.getProperty("output"));
                report = Integer.decode(p.getProperty("report"));
                int verify = Integer.decode(p.getProperty("verify"));
                int cpu = Integer.parseInt(p.getProperty("cpu"));
                int ram = Integer.parseInt(p.getProperty("ram"));
                boolean fault = Boolean.parseBoolean(p.getProperty("fault"));
                int target = cpu == 3 ? 0x100 : 0x1000;
                long deadline = System.nanoTime() + 600_000_000_000L;
                while (XEiJ.mpuTask == null) {
                    require(System.nanoTime() < deadline, "Emulator did not start");
                    Thread.sleep(20);
                }
                // The emulator timer serializes this with execution of its CPU core.
                XEiJ.tmrTimer.schedule(new TimerTask() {
                    public void run() { InstructionBreakPoint.ibpInstant(verify, 0x2000); }
                }, 0);
                boolean configured = false;
                while (true) {
                    require(System.nanoTime() < deadline, "Timed out waiting for a complete pass");
                    if (!observed && XEiJ.mpuTask == null && XEiJ.regPC == verify) {
                        require(word(0xe5ff28) == 1, "Probe is not running");
                        require(word(0xe5ff18) == (cpu == 3 ? 0 : 0x400), "Wrong actual probe start");
                        require(XEiJ.regRn[8] == (cpu == 3 ? 0 : 0x400), "Wrong verifier A0");
                        require(XEiJ.mpuVBR == (cpu == 3 ? 0xe41000 : 0), "Wrong actual VBR");
                        require(word(target) == 0, "First fill pattern was not written");
                        if (fault) { MainMemory.mmrM8[target+3] ^= 1; injected = true; }
                        observed = true;
                        System.out.println("headless: observed probe; fault=" + injected);
                        SwingUtilities.invokeAndWait(() -> {
                            InstructionBreakPoint.ibpRemove(verify, 0x2000);
                            XEiJ.mpuStart();
                        });
                    }
                    if (word(report) == 0x4d543638) {
                        if (!configured && observed) {
                            key("c"); key("l"); key("return");
                            configured = true;
                            System.out.println("headless: selected one pass through configuration keys");
                        }
                        require(word(report+16*4) != 4, "Target aborted the probe");
                        if (word(report+16*4) == 3) {
                            require(configured && observed, "Pass completed without observation");
                            int[] expected = {0x4d543638, 1, cpu, ram, cpu == 3 ? 0 : 1024,
                                cpu == 3 ? 65536 : 64512, ram - (cpu == 3 ? 0 : 1024),
                                1, fault ? 1 : 0, 1, fault ? 1 : 0, 0, 0,
                                cpu == 3 ? 0xe41000 : 0, 0, 0, 3, 255, fault ? target : 0};
                            for (int i = 0; i < expected.length; i++)
                                require(word(report+i*4) == expected[i], FIELDS[i] + ": got " +
                                    Integer.toUnsignedString(word(report+i*4)) + ", expected " + expected[i]);
                            require(!fault || injected, "Fault was not injected");
                            require(XEiJ.mpuVBR == word(report+14*4), "VBR not restored in CPU");
                            break;
                        }
                    }
                    Thread.sleep(20);
                }
                Thread.sleep(300);
            } catch (Throwable e) {
                e.printStackTrace();
                failure = e.toString();
            }
            try {
                if (output != null) {
                    screen(output.resolve("screen.png"));
                    StringBuilder json = new StringBuilder("{\n  \"success\": ").append(failure == null)
                        .append(",\n  \"observed_probe\": ").append(observed)
                        .append(",\n  \"injected\": ").append(injected);
                    if (failure != null) json.append(",\n  \"failure\": \"")
                        .append(failure.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")).append('"');
                    if (MainMemory.mmrM8 != null && report > 0)
                        for (int i = 0; i < FIELDS.length; i++)
                            json.append(",\n  \"").append(FIELDS[i]).append("\": ")
                                .append(Integer.toUnsignedString(word(report+i*4)));
                    Files.writeString(output.resolve("result.json"), json.append("\n}\n"));
                }
            } catch (Throwable e) { e.printStackTrace(); failure = e.toString(); }
            System.exit(failure == null ? 0 : 1);
        }, "memtest68-headless-verification");
        thread.setDaemon(true);
        thread.start();
    }
}
