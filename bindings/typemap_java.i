/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 * Author: NikitaFeodonit, nfeodonit@yandex.com
 ******************************************************************************
 *   Copyright (c) 2016 NextGIS, <info@nextgis.com>
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 32 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

%include "typemaps.i"
%include "enumtypeunsafe.swg" // for use in cases

%module Api

%javaconst(1);

%rename (ErrorCodes) ngsErrorCodes;
%rename (SourceCodes) ngsSourceCodes;
%rename (ChangeCodes) ngsChangeCodes;
%rename (Colour) _ngsRGBA;
%rename (RawPoint) _ngsRawPoint;
%rename (RawEnvelope) _ngsRawEnvelope;

// Typemaps for (void* imageBufferPointer)
%typemap(in, numinputs=1) (void* imageBufferPointer)
{
    if ($input == 0)
    {
        SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null array");
        return $null;
    }
    $1 = jenv->GetDirectBufferAddress($input);
    if ($1 == NULL)
    {
        SWIG_JavaThrowException(jenv, SWIG_JavaRuntimeException,
                                "Unable to get address of direct buffer. Buffer must be allocated direct.");
        return $null;
    }
}


/* These 3 typemaps tell SWIG what JNI and Java types to use */
%typemap(jni) (void* imageBufferPointer)  "jobject"
%typemap(jtype) (void* imageBufferPointer)  "java.nio.ByteBuffer"
%typemap(jstype) (void* imageBufferPointer)  "java.nio.ByteBuffer"
%typemap(javain) (void* imageBufferPointer)  "$javainput"
%typemap(javaout) (void* imageBufferPointer) {
    return $jnicall;
}
