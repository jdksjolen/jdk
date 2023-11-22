package org.openjdk.bench.vm.runtime;

import org.openjdk.jmh.annotations.*;
import org.openjdk.jmh.infra.Blackhole;

import java.util.concurrent.TimeUnit;

import jdk.internal.misc.Unsafe;
import jdk.test.whitebox.WhiteBox;

public class NMTBenchmark_wb {
  private static final int K = 1024 * 4;
  private static final WhiteBox wb = WhiteBox.getWhiteBox();

  private static long reserve (long size           ) { return wb.NMTReserveMemory (size               ); }
  private static void commit  (long base, int r    ) {        wb.NMTCommitMemory  (base + r * K, K    ); }
  private static void uncommit(long base, int r    ) {        wb.NMTUncommitMemory(base + r * K, K    ); }
  private static void release (long base, long size) {        wb.NMTReleaseMemory (base        , size ); }

  public static void doAllMemoryOps(int region_count) {
    long region_size = region_count * K;
    long base = reserve(region_size);
    for (int i = 0; i < region_count; i+=4) commit  (base, i    );
    for (int i = 1; i < region_count; i+=4) commit  (base, i    ); // causes merge from right
    for (int i = 4; i < region_count; i+=4) commit  (base, i - 1); // causes merge from left
    for (int i = 4; i < region_count; i+=4) uncommit(base, i - 1); // causes split from left
    for (int i = 1; i < region_count; i+=4) uncommit(base, i    ); // causes split from right
    for (int i = 0; i < region_count; i+=4) uncommit(base, i    ); // remove the regions
    release(base, region_size);
  }
  public static void doTest(int nR, int nT) throws InterruptedException{
    Thread[] threads =  new Thread[nT];
    for (int t=0; t< nT; t++) {
      threads[t] = new Thread(() -> doAllMemoryOps(nR));
      threads[t].start();
    }
    for (Thread t: threads) t.join();
  }
}
