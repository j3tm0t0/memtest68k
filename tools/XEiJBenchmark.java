import java.lang.instrument.Instrumentation;
import java.nio.file.Files;
import java.nio.file.Path;
import xeij.MainMemory;

public class XEiJBenchmark {
    static int word(int address) {
        byte[] m = MainMemory.mmrM8;
        return (m[address]&255)<<24 | (m[address+1]&255)<<16 | (m[address+2]&255)<<8 | m[address+3]&255;
    }
    public static void premain(String argument, Instrumentation instrumentation) {
        String[] args = argument.split(",",2);
        int address = Integer.decode(args[0]);
        Thread thread = new Thread(() -> {
            try {
                long deadline = System.nanoTime()+180_000_000_000L;
                while (System.nanoTime()<deadline) {
                    if (MainMemory.mmrM8 != null && word(address)==0x424d3638 && word(address+4)>=2) {
                        if (word(address+4)!=2 || word(address+16)!=0) throw new AssertionError("Benchmark found errors");
                        StringBuilder json = new StringBuilder("{\"bytes\": "+word(address+8)+", \"clock_mhz\": 16, \"centiseconds\": [");
                        int count=word(address+12)*7;
                        for(int i=0;i<count;i++) json.append(i==0?"":",").append(word(address+20+i*4));
                        Files.writeString(Path.of(args[1]),json.append("]}\n"));
                        System.exit(0);
                    }
                    Thread.sleep(20);
                }
                throw new AssertionError("Benchmark timed out");
            } catch (Throwable e) { e.printStackTrace(); System.exit(1); }
        },"memtest68-benchmark");
        thread.setDaemon(true); thread.start();
    }
}
