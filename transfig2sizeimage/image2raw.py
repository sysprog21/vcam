from PIL import Image
input1 = input("please enter the image ")
if input1.endswith('.jpg'):
    img = Image.open(input1)
    new_img = img.resize((640, 480))
    new_img.save("../2.jpg", "JPEG", optimize=True)
    im = Image.open('../2.jpg')
    pix = im.load()
    f = open("../1.raw", "w")
    for j in range(480):
        for i in range(640):
            f.write(str(pix[i,j][0])+' '+str(pix[i,j][1])+' '+str(pix[i,j][2])+'\n')
    f.close()
else:
    img = Image.open(input1)
    new_img = img.resize((640, 480))
    new_img.save("../2.jpeg", "JPEG", optimize=True)
    im = Image.open('../2.jpeg')
    pix = im.load()
    f = open("../1.raw", "w")
    for j in range(480):
        for i in range(640):
            f.write(str(pix[i,j][2])+' '+str(pix[i,j][1])+' '+str(pix[i,j][0])+'\n')
    f.close() 

