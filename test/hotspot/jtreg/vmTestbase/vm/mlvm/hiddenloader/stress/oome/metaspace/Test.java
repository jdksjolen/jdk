/*
 * Copyright (c) 2010, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */


/*
 * @test
 * @modules java.base/jdk.internal.misc
 *
 * @summary converted from VM Testbase vm/mlvm/anonloader/stress/oome/metaspace.
 * VM Testbase keywords: [feature_mlvm, nonconcurrent]
 *
 * @library /vmTestbase
 *          /test/lib
 *
 * @comment build test class and indify classes
 * @build vm.mlvm.hiddenloader.stress.oome.metaspace.Test
 * @run driver vm.mlvm.share.IndifiedClassesBuilder
 *
 * @run main/othervm -XX:MaxRAMPercentage=25 -XX:-UseGCOverheadLimit -XX:MetaspaceSize=10m
 *                   -XX:MaxMetaspaceSize=20m vm.mlvm.hiddenloader.stress.oome.metaspace.Test
 */

package vm.mlvm.hiddenloader.stress.oome.metaspace;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodHandles.Lookup;
import java.util.List;
import java.io.IOException;

import vm.mlvm.hiddenloader.share.HiddenkTestee01;
import vm.mlvm.share.MlvmOOMTest;
import vm.mlvm.share.MlvmTestExecutor;
import vm.mlvm.share.Env;
import vm.mlvm.share.FileUtils;

/**
 * This test loads classes using defineHiddenClass and stores them,
 * expecting Metaspace OOME.
 *
 */
public class Test extends MlvmOOMTest {
    @Override
    protected void checkOOME(OutOfMemoryError oome) {
        String message = oome.getMessage();
        if (!"Metaspace".equals(message) && !"Compressed class space".equals(message)) {
            throw new RuntimeException("TEST FAIL : wrong OOME", oome);
        }
    }

    @Override
    protected void eatMemory(List<Object> list) {
        byte[] classBytes = null;
        try {
            classBytes = FileUtils.readClass(HiddenkTestee01.class.getName());
        } catch (IOException e) {
            Env.throwAsUncheckedException(e);
        }
        try {
            while (true) {
                Lookup lookup = MethodHandles.lookup();
                Lookup ank_lookup = MethodHandles.privateLookupIn(HiddenkTestee01.class, lookup);
                Class<?> c = ank_lookup.defineHiddenClass(classBytes, true).lookupClass();
                list.add(c.newInstance());
             }
         } catch (InstantiationException | IllegalAccessException e) {
             Env.throwAsUncheckedException(e);
         }
    }

    public static void main(String[] args) {
        MlvmTestExecutor.launch(args);
    }
}
