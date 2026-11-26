/* Optional development bridge. Uses XEiJ's public Java APIs to capture the
 * rendered emulator panel and send keys, without capturing the host desktop.
 * Usage: java -javaagent:bridge.jar=/absolute/path/command.txt -jar XEiJ.jar ...
 * Commands: screen /absolute/path.png | key c | key esc | regs | exit
 *           dump ADDRESS LENGTH /absolute/path.bin
 *           fault-low VERIFY_PC END_PC (one-shot low-RAM fault injection)
 */
import java.awt.Graphics2D;
import java.awt.image.BufferedImage;
import java.lang.instrument.Instrumentation;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import javax.imageio.ImageIO;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;

public class XEiJBridge {
    public static void premain(String argument, Instrumentation instrumentation) {
        Path command = Path.of(argument);
        Thread thread = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep(200);
                    if (!Files.exists(command)) continue;
                    List<String> lines = Files.readAllLines(command);
                    Files.delete(command);
                    for (String line : lines) {
                        String[] parts = line.trim().split("\\s+", 2);
                        Class<?> machine = Class.forName("xeij.XEiJ");
                        if (parts[0].equals("screen")) {
                            SwingUtilities.invokeAndWait(() -> {
                                try {
                                    JPanel panel = (JPanel)machine.getField("pnlPanel").get(null);
                                    BufferedImage image = new BufferedImage(panel.getWidth(), panel.getHeight(),
                                            BufferedImage.TYPE_INT_RGB);
                                    Graphics2D graphics = image.createGraphics();
                                    panel.paint(graphics);
                                    graphics.dispose();
                                    ImageIO.write(image, "png", Path.of(parts[1]).toFile());
                                    System.out.println("bridge: saved " + parts[1]);
                                } catch (Exception exception) { exception.printStackTrace(); }
                            });
                        } else if (parts[0].equals("key")) {
                            Map<?,?> keys = (Map<?,?>)Class.forName("xeij.CONDevice")
                                    .getField("conKeyMap").get(null);
                            int code = (Integer)keys.get(parts[1]);
                            Class<?> keyboard = Class.forName("xeij.Keyboard");
                            keyboard.getMethod("kbdCommandPress", int.class).invoke(null, code);
                            Thread.sleep(80);
                            keyboard.getMethod("kbdCommandRelease", int.class).invoke(null, code);
                            System.out.println("bridge: key " + parts[1]);
                        } else if (parts[0].equals("regs")) {
                            System.out.println("bridge: PC=" + machine.getField("regPC").get(null) + " R=" +
                                    Arrays.toString((int[])machine.getField("regRn").get(null)));
                        } else if (parts[0].equals("dump")) {
                            String[] dump = parts[1].split("\\s+");
                            int start = Integer.decode(dump[0]), length = Integer.decode(dump[1]);
                            byte[] memory = (byte[])Class.forName("xeij.MainMemory")
                                    .getField("mmrM8").get(null);
                            Files.write(Path.of(dump[2]), Arrays.copyOfRange(memory, start, start+length));
                        } else if (parts[0].equals("fault-low")) {
                            String[] range = parts[1].split("\\s+");
                            int first = Integer.decode(range[0]), last = Integer.decode(range[1]);
                            Thread injector = new Thread(() -> {
                                try {
                                    byte[] memory = (byte[])Class.forName("xeij.MainMemory").getField("mmrM8").get(null);
                                    long deadline = System.nanoTime() + 60_000_000_000L;
                                    while (System.nanoTime() < deadline) {
                                        int pc = machine.getField("regPC").getInt(null);
                                        int[] regs = (int[])machine.getField("regRn").get(null);
                                        if (pc >= first && pc < last && regs[6] == 0 && regs[8] < 0xfffc) {
                                            memory[0xffff] ^= 1;
                                            System.out.println("bridge: injected low-RAM fault at $0000FFFF");
                                            return;
                                        }
                                        Thread.sleep(0, 100000);
                                    }
                                    System.out.println("bridge: low-RAM injection timed out");
                                } catch (Exception exception) { exception.printStackTrace(); }
                            }, "memtest68-fault-injection");
                            injector.setDaemon(true);
                            injector.start();
                        } else if (parts[0].equals("exit")) {
                            SwingUtilities.invokeAndWait(() -> {
                                try { machine.getMethod("prgTini").invoke(null); }
                                catch (Exception exception) { exception.printStackTrace(); }
                            });
                        }
                    }
                } catch (Exception exception) { exception.printStackTrace(); }
            }
        }, "memtest68-development-bridge");
        thread.setDaemon(true);
        thread.start();
    }
}
