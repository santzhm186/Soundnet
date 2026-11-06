#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
int main(int argc,char **argv){if(argc!=2)return 1;@autoreleasepool{
CGColorSpaceRef cs=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
CGContextRef c=CGBitmapContextCreate(NULL,1024,1024,8,4096,cs,kCGImageAlphaPremultipliedLast);
CGPathRef p=CGPathCreateWithRoundedRect(CGRectMake(66,66,892,892),208,208,NULL);
CGContextAddPath(c,p);CGContextClip(c);
CGFloat bg[]={.22,.255,.25,1,.075,.09,.09,1};CGGradientRef g=CGGradientCreateWithColorComponents(cs,bg,NULL,2);
CGContextDrawLinearGradient(c,g,CGPointMake(320,980),CGPointMake(710,30),0);
CGContextSetLineCap(c,kCGLineCapRound);CGContextSetLineWidth(c,64);
CGFloat accent[][3]={{1,.66,.40},{.57,.86,.74},{.76,.71,.98}};
for(int j=0;j<3;j++){CGContextSetRGBStrokeColor(c,accent[j][0],accent[j][1],accent[j][2],1);
CGContextMoveToPoint(c,235+j*191,338);CGContextAddLineToPoint(c,409+j*191,686);CGContextStrokePath(c);}
CGImageRef img=CGBitmapContextCreateImage(c);CGImageDestinationRef dst=CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:@(argv[1])],CFSTR("public.png"),1,NULL);
CGImageDestinationAddImage(dst,img,NULL);return !CGImageDestinationFinalize(dst);}}
