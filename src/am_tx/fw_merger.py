import sys

def merge_files(in1, in2, out):
    with open(in1, 'rb') as f1:
        f2 = open(in2, 'rb')
        with open(out, 'wb') as fo:
            fo.write(f1.read())
            fo.write(f2.read())
    f2.close() 

if __name__ == '__main__':
   args = sys.argv
   merge_files(args[1], args[2], args[3])