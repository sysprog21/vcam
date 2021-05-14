from PIL import Image
img = Image.open('../sample_image/1.jpg')
new_img = img.resize((640, 480))
new_img.save("../sample_image/2.jpg", "JPEG", optimize=True)

im = Image.open('../sample_image/2.jpg')
pix = im.load()
f = open("../sample_image/1.raw", "w")
for j in range(480):
    for i in range(640):
        f.write(str(pix[i,j][0])+' '+str(pix[i,j][1])+' '+str(pix[i,j][2])+'\n')
f.close()
