function [Y1,U1,V1,Y2,U2,V2] = process_hdmi_yuv(Y1,U1,V1,Y2,U2,V2)
    [Y1,U1,V1] = process_hdmi_1(Y1,U1,V1);
    [Y2,U2,V2] = process_hdmi_1(Y2,U2,V2);
end

function [Y,U,V] = process_hdmi_1(Y,U,V)
    global HDMI_PROCESS_YUV_BLUR_SHARPEN, global HDMI_PROCESS_YUV_NOISE, global HDMI_PROCESS_RGB_COMPRESS

    % convert the image to YUV422 8-bit (16-235)
    [Y,U,V] = yuv4442yuv422(Y,U,V);
   
    % simulate some image filters that might be applied during recording
    if HDMI_PROCESS_YUV_BLUR_SHARPEN
        Y = imfilter(Y, fspecial('disk',1));
        Y = imfilter(Y, fspecial('unsharp'));
        U = imfilter(U, fspecial('disk',1));
        V = imfilter(V, fspecial('disk',1));
    end
   
    if HDMI_PROCESS_YUV_NOISE
       Y = uint8(double(Y) + randn(size(Y)));
       U = uint8(double(U) + randn(size(U)));
       V = uint8(double(V) + randn(size(V)));
    end
   
    % back to full-res YUV
    [Y,U,V] = yuv4222yuv444(Y,U,V);
   
    if HDMI_PROCESS_RGB_COMPRESS
        disp('FIXME: YUV compression not implemented')
    end
end

function [Y,U,V] = yuv4442yuv422(Y,U,V)
    U = imresize(U, [size(U,1) size(U,2)/2]);
    V = imresize(V, [size(V,1) size(V,2)/2]);
end

function [Y,U,V] = yuv4222yuv444(Y,U,V)
    U = imresize(U, [size(U,1) size(U,2)*2]);
    V = imresize(V, [size(V,1) size(V,2)*2]);
    U = uint8(U);
    V = uint8(V);
    Y = uint8(Y);
end
