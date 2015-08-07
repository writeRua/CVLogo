#include "cds.h"
using namespace std;
using namespace cv;
CDS::CDS():alpha(20),beta(20),tau(0.4),Nr(10),eps(1e-3),eeps(1e-13),
	maxtheta(16),maxrho(16),maxt(30),threshold(0.90),responseThresholdx(0.09),responseThresholdy(0.05),rectangleThreshold(0.95)
{
	for(int i = 0; i < maxtheta; i++)
	{
		P.push_back(vector<Mat>(maxrho));
		Q.push_back(vector<Mat>(maxrho));
	}
	param = (Param){alpha, beta, tau, Nr, eps, eeps,maxtheta, maxrho, maxt,threshold, responseThresholdx,responseThresholdy, rectangleThreshold};
}

void CDS::extractSIFTKeyPoint() {
	SiftFeatureDetector sift; //此处先用默认参数
	std::vector<KeyPoint> Sx_tmp, Sy_tmp;
	sift.detect(Ix, Sx_tmp);
	sift.detect(Iy, Sy_tmp);
	for(int i = 0; i < (int)Sx_tmp.size(); i++)
	{
		KeyPoint & key = Sx_tmp[i];
		if(key.response > responseThresholdx)Sx.push_back(key);
	}
	for(int i = 0; i < (int)Sy_tmp.size(); i++)
	{
		KeyPoint & key = Sy_tmp[i];
		if(key.response > responseThresholdy)Sy.push_back(key);
	}

	SiftDescriptorExtractor siftDesc;//定义描述子对象

	siftDesc.compute(Ix,Sx,Dx);//计算特征向量
	siftDesc.compute(Iy,Sy,Dy);

	D = Mat::zeros(Sx.size(),Sy.size(),CV_32FC1);
	for(int i = 0; i < Dx.rows; i++)
		for(int j = 0; j < Dy.rows; j++)
		{
			const float* px = Dx.ptr<float>(i);
			const float* py = Dy.ptr<float>(j);
			for(int k = 0; k < Dx.cols; k++)
			{
				float tmp = *px - *py;
				D.at<float>(i,j) += tmp * tmp;
				px++;
				py++;
			}
			D.at<float>(i,j) = sqrt(D.at<float>(i,j));
		}
}

void CDS::computeContext() {
	for(int i = 0; i < maxtheta; i++)
		for(int j = 0; j < maxrho; j++)
		{
			P[i][j] = Mat::zeros(Sx.size(),Sx.size(),CV_32FC1);
			Q[i][j] = Mat::zeros(Sy.size(),Sy.size(),CV_32FC1);
		}
	for(int i=0;i< (int) Sx.size();i++) //Sx.size() == n in the paper
	{
		float deltarho = (float) Sx[i].size / Nr;
		float baseangle = Sx[i].angle;
		float deltaangle = 360.0 / maxtheta;
		for(int j = 0; j < (int) Sx.size(); j++)
			if(i !=j && unpackSIFTOctave(Sx[i]) == unpackSIFTOctave(Sx[j]) && dist(Sx[i],Sx[j]) < maxrho * deltarho)  //此处8为硬编码，因为我也不知道这里到底该咋办
			{
				float tmp = Sx[j].angle - baseangle;
				if(tmp < 0) tmp += 360 ; //keep tmp in [0, 2*PI]
				int theta = (int) (tmp / deltaangle);
				int rho = (int) ( (dist(Sx[i],Sx[j])) / deltarho );
				P[theta][rho].at<float>(i,j) = 1;
			}
	}

	for(int i=0;i< (int) Sy.size();i++) //Sy.size() == m in the paper
	{
		float deltarho = (float) Sy[i].size / Nr;
		float baseangle = Sy[i].angle;
		float deltaangle = 360.0 / maxtheta;
		for(int j = 0; j < (int) Sy.size(); j++)
			if(i != j && unpackSIFTOctave(Sy[i]) == unpackSIFTOctave(Sy[j]) && dist(Sy[i],Sy[j]) < maxrho * deltarho)  //此处8为硬编码，因为我也不知道这里到底该咋办
			{
				float tmp = Sy[j].angle - baseangle;
				if(tmp < 0) tmp += 360 ; //keep tmp in [0, 2*PI]
				int theta = (int) (tmp / deltaangle);
				int rho = (int) ( (dist(Sy[i],Sy[j])) / deltarho );
				Q[theta][rho].at<float>(i,j) = 1;
			}
	}
}

float CDS::dist(const KeyPoint &p1, const KeyPoint &p2) const
{
	float deltax = p1.pt.x - p2.pt.x , deltay = p1.pt.y - p2.pt.y;
	return sqrt(deltax*deltax + deltay*deltay);
}

void CDS::computeCDSMatrix()
{
	Mat K_history;
	K_history = D * (-1.0 / beta);
	for(int i = 0; i < K_history.rows; i++)
	{
		float* now = K_history.ptr<float>(i);
		for(int j = 0; j < K_history.cols; j++)
		{
			(*now) = exp(*now);
			now++;
		}
	}
	cout << norm(K_history) << endl;
	if (norm(K_history) < eeps)
	{
		K = K_history.clone();
		return;
	}
	normalize(K_history, K_history, 1, 0, NORM_L1);
	for(int t = 0; t < maxt; t++)
	{
		cout << "============================== iterator num = " << t << " ===========================" << endl;
		cout << norm(K_history) << endl;
		if(norm(K_history) < eeps)break;
		normalize(G(K_history),K, 1, 0, NORM_L1);
		cout << norm(K) << endl;
		if(isnan(norm(K)))
		{
			K = K_history.clone();
			break;
		}
		if( norm(K - K_history) < eps ) break;
		K_history = K.clone();
	}
}

Mat CDS::G(const Mat &K)const
{
	Mat ret(Sx.size(), Sy.size(), CV_32FC1);
	for(int theta = 0; theta < maxtheta; theta++)
		for(int rho = 0; rho < maxrho; rho++)
		{
			ret += P[theta][rho] * K * Q[theta][rho].t()
					+ P[theta][rho].t() * K * Q[theta][rho];
		}
	ret = D / (-1.0 * beta) + ret * (alpha/beta);

	for(int i = 0; i < ret.rows; i++)
	{
		float* now = ret.ptr<float>(i);
		for(int j = 0; j < ret.cols; j++)
		{
			(*now) = exp(*now);
			if(isnan(*now)) *now = 0;
			now++;
		}
	}
	return ret;
}


bool CDS::match()
{
	Kp = Mat(Sx.size(),Sy.size(),CV_32FC1); //条件概率 j|i
	vector<float> sumK;  //当x=i时的和
	sumK.clear();
	for(int i = 0; i < (int)Sx.size(); i++)
	{
		sumK.push_back(0);
		for(int j = 0; j < (int)Sy.size(); j++)
		{
			sumK[i] +=  K.at<float>(i,j);
		}
	}

	for(int i = 0; i < (int)Sx.size(); i++)
		for(int j = 0; j < (int)Sy.size(); j++)
		{
			Kp.at<float>(i,j) = K.at<float>(i,j) / sumK[i];
		}
	matchY.clear();

	vector<float> sumKp;
	for(int i = 0; i < (int)Sx.size(); i++)
	{
		sumKp.push_back(0);
		for(int  j = 0; j < (int)Sy.size(); j++)
			sumKp[i] += Kp.at<float>(i,j);
	}

	for(int i = 0; i < (int)Sx.size(); i++)
		for(int j = 0; j < (int)Sy.size(); j++)
			if(Kp.at<float>(i,j) >= sumKp[i] * threshold)
			{
				matchVec.push_back(DMatch(i,j,0,Kp.at<float>(i,j) / sumKp[i]));
				if(matchY.find(j) == matchY.end()) matchY.insert(j);
			}
	cout << "match pair = " << matchY.size() <<endl;
	cout << "key_point_num = " << Sx.size() << endl;
	cout << "maxTau = " << matchY.size() << "/" << Sx.size() << " = " << (float) matchY.size() / Sx.size() << endl;
	if(matchY.size() > tau * Sx.size()) return true;
	return false;
}

void CDS::unpackSIFTOctave(const KeyPoint& kpt, int& octave, int& layer, float& scale)
{
	octave = kpt.octave & 255;
	layer = (kpt.octave >> 8) & 255;
	octave = octave < 128 ? octave : (-128 | octave);
	scale = octave >= 0 ? 1.f/(1 << octave) : (float)(1 << -octave);
}

int CDS::unpackSIFTOctave(const KeyPoint &kpt)
{
	int octave = kpt.octave & 255;
	octave = octave < 128 ? octave : (-128 | octave);
	return octave;
}

void CDS::visualize()
{
	drawMatches(Ix, Sx, Iy, Sy, matchVec, showImg);
	namedWindow("showImg", 1);
	//setMouseCallback("showImg", CDS::callBackFuncForROI, NULL);
	for(int i = 0; i < (int)matchVec.size() ; i+=1)
	{
		vector<DMatch> tmp;
		tmp.clear();
		for(int j = i; j < std::min((int)i+1,(int)matchVec.size()); j++)
		{
			tmp.push_back(matchVec[j]);
		}
		drawMatches(Ix, Sx, Iy, Sy, tmp, showImg);
		imshow("showImg", showImg);
		waitKey(100);
	}
	//resize(showImg,showImg,Size(showImg.cols*2.5,showImg.rows*2.5));
	drawMatches(Ix, Sx, Iy, Sy, matchVec, showImg);
	imshow("showImg", showImg);
	waitKey(1000);
}


void CDS::callBackFuncForROI(int event, int x, int y, int flags, void* userdata)
{
	if(flags == 0 && userdata)
	{
		//I only want to use these parameters...
	}
	if( event == EVENT_LBUTTONDOWN)
	{
		cout << "Left button of the mouse is down - position (" << x << "," << y << ")" << endl;
	}
	else if( event == EVENT_LBUTTONUP)
	{
		cout << "Left button of the mouse is up - position (" << x << "," << y << ")" << endl;
	}
}
bool CDS::process()
{
	extractSIFTKeyPoint();
	computeContext();
	computeCDSMatrix();
	return match();
}

void CDS::drawRectanglePlanA()
{
	Point2f RecPt1 = Point2f(0,0), RecPt2 = Point2f(0,0);
	int cnt=0;
	for(int i = 0; i < (int)matchVec.size(); i++) if(matchVec[i].distance > rectangleThreshold)
	{
		cout << i << endl;
		float upx, downx, leftx, rightx, upy, downy, lefty, righty;
		const KeyPoint &Px = Sx[matchVec[i].queryIdx], &Py = Sy[matchVec[i].trainIdx];

		upx = Px.pt.y;
		leftx = Px.pt.x;
		downx = Ix.rows - upx;
		rightx = Ix.cols - leftx;

		float ratio = Py.size / Px.size;
		upy    = ratio * upx;
		lefty  = ratio * leftx;
		downy  = ratio * downx;
		righty = ratio * rightx;

		Point2f p1 = Py.pt - Point2f(lefty , upy  );
		Point2f p2 = Py.pt + Point2f(righty, downy);
		RecPt1 += p1;
		RecPt2 += p2;
		cnt ++;
	}
	RecPt1.x /= cnt;
	RecPt1.y /= cnt;
	RecPt2.x /= cnt;
	RecPt2.y /= cnt;

	rectangle(Iy, Rect(RecPt1, RecPt2), Scalar(0));
	imshow("Rectangle", Iy);
	waitKey(10);
}

void CDS::drawRectanglePlanB()
{
	float maxdistance = 0;
	int id;
	for(int i = 0; i < (int)matchVec.size(); i++)
		if(matchVec[i].distance > maxdistance)
		{
			id = i;
			maxdistance = matchVec[i].distance;
		}

	cout << id << endl;

	float upx, downx, leftx, rightx, upy, downy, lefty, righty;
	const KeyPoint &Px = Sx[matchVec[id].queryIdx], &Py = Sy[matchVec[id].trainIdx];

	upx = Px.pt.y;
	leftx = Px.pt.x;
	downx = Ix.rows - upx;
	rightx = Ix.cols - leftx;

	float ratio = Py.size / Px.size;
	upy    = ratio * upx;
	lefty  = ratio * leftx;
	downy  = ratio * downx;
	righty = ratio * rightx;

	Point2f RecPt1 = Py.pt - Point2f(lefty , upy  );
	Point2f RecPt2 = Py.pt + Point2f(righty, downy);

	rectangle(Iy, Rect(RecPt1, RecPt2), Scalar(0));
	imshow("Rectangle", Iy);
	waitKey(10);
}

void CDS::setParameters(const Param &param)
{
	this->param = param;
	alpha     = param.alpha;
	beta      = param.beta;
	tau       = param.tau;
	Nr        = param.Nr;
	eps       = param.eps;  // 判断矩阵是否为零 1e-2
	eeps      = param.eeps; // 防止归一化零矩阵 1e-12
	maxtheta  = param.maxtheta;
	maxrho    = param.maxrho;
	maxt      = param.maxt;
	threshold = param.threshold;
	responseThresholdx = param.responseThresholdx;
	responseThresholdy = param.responseThresholdy;
	rectangleThreshold = param.rectangleThreshold;
}

void CDS::writeParameters(ofstream &outfile)
{
	char* Param_char = (char*)&param;
	cout << sizeof(param) << endl;
	outfile.write(Param_char,sizeof(param));
}

void CDS::readParameters(ifstream &infile)
{
	char* Param_char = (char*)&param;
	infile.read(Param_char,sizeof(param));
}

void CDS::writeLogo(ofstream &outfile)
{
	char* Logo_char = (char*)&Ix;
	outfile.write(Logo_char,sizeof(Ix));
}

void CDS::getSiftKeyPoint(std::vector<KeyPoint> &Sx, std::vector<KeyPoint> &Sy)
{
	Sx = this->Sx;
	Sy = this->Sy;
}
