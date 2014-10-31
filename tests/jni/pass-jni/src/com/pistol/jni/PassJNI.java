package com.pistol.jni;

import com.pistol.jni.R;
import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.widget.TextView;
import java.util.Arrays;

import dalvik.system.Taint;

public class PassJNI extends Activity
{
    Button buttonStash;
    Button buttonProcess;
    Button buttonMalloc;
    Button buttonMatrix;
    Button buttonMatrixBasic;
    Button buttonTaintedInt;
    TextView tv;
 
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.main);
        
        tv = new TextView(this);
        addListenerOnButton();

    }

    public void addListenerOnButton() {
        buttonStash = (Button) findViewById(R.id.buttonStash);
        buttonStash.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View arg0) {
                /* Create a TextView and set its content.
                 * the text is retrieved by calling a native
                 * function.
                 */
                password = "elitep4ss";
                stash = "nothing";
                
                Taint.addTaintString(password, Taint.TAINT_PASSWORD);
                
                stashPasswordField();
                tv.setText(stash);
                setContentView(tv);
            
                // System.out.println("Stash: " + stash);
                // System.out.println("Password: " + password);
            }
        });

        buttonProcess = (Button) findViewById(R.id.buttonProcess);
        buttonProcess.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View arg0) {
                password = "elitep4ss";
                
                Taint.addTaintString(password, Taint.TAINT_PASSWORD);
                
                processPassword(password);
                tv.setText(password);
                setContentView(tv);
            }
        });


        buttonMalloc = (Button) findViewById(R.id.buttonMalloc);
        buttonMalloc.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View arg0) {
                /* Create a TextView and set its content.
                 * the text is retrieved by calling a native
                 * function.
                 */
                password = "elitep4ss";
                stash = "nothing";

                Taint.addTaintString(password, Taint.TAINT_PASSWORD);

                memcpyPassword();
                tv.setText(stash);
                setContentView(tv);

                editCopy();

                // System.out.println("Stash: " + stash);
                // System.out.println("Password: " + password);
            }
        });

        buttonMatrix = (Button) findViewById(R.id.buttonMatrix);
        buttonMatrix.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View arg0) {
                /* Create a TextView and set its content.
                 * the text is retrieved by calling a native
                 * function.
                 */

                // matrixInput1 = [ ];
                // matrixInput2 = [ ];
                int[] a = {1, 2, 3, 4, 5, 6, 7, 8, 9};
                int[] b = {9, 8, 7, 6, 5, 4, 3, 2, 1};
                int size = 3;

                // Taint.addTaintByteArray(password, Taint.TAINT_PASSWORD);

                int[] result;
                result = matrixMul(size, a, b);
                String resultString = Arrays.toString(result);
                tv.setText(resultString);
                setContentView(tv);

                System.out.println("Result: " + resultString);
            }
        });

        buttonMatrixBasic = (Button) findViewById(R.id.buttonMatrixBasic);
        buttonMatrixBasic.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View arg0) {
                matrixMulBasic();
                tv.setText("Done");
                setContentView(tv);
            }
        });

        buttonTaintedInt = (Button) findViewById(R.id.buttonTaintedInt);
        buttonTaintedInt.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View arg0) {
                int input = 1337;
                input = Taint.addTaintInt(input, Taint.TAINT_PASSWORD);

                processTaintedInt(input);
                tv.setText("Done");
                setContentView(tv);
            }
        });
    }
    
    String password;
    String stash;

    // print and secretly copy(stash) 'password' String
    public native void stashPasswordField();

    // read/modify tainted password given as input
    public native void processPassword(String password);
    
    // simple taint test
    public native void processTaintedInt(int input);

    // dump password in a native malloc buffer
    public native void memcpyPassword();

    // matrix multiply on tainted data
    public native int[] matrixMul(int size, int[] a, int[] b);

    // basic matrix multiply for emu baseline
    public native void matrixMulBasic();

    // modify the stashed (via malloc) password
    public native void editCopy();

    /* this is used to load the 'pass-jni' library on application
     * startup. The library has already been unpacked into
     * /data/data/com.pistol.jni/lib/libpass-jni.so at
     * installation time by the package manager.
     */
    static {
        System.loadLibrary("pass-jni");
    }
}
