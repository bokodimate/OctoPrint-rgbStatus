#include "RgbLightHandler.h"

RgbLightHandler::RgbLightHandler(vector<float> defaultColor)
{
	patternLeft = new RgbLightConstant(defaultColor);
	patternRight = new RgbLightConstant(defaultColor);

	patternsChanged = false;
	pwmDriver = new PwmDriver();

	transitionsEnabled = false;
}

RgbLightHandler::RgbLightHandler(vector<float> defaultColor, unsigned int transitionRefreshInterval, unsigned int transitionTime)
{
	patternLeft = new RgbLightConstant(defaultColor);
	patternRight = new RgbLightConstant(defaultColor);

	patternsChanged = false;
	pwmDriver = new PwmDriver();

	transitionsEnabled = true;
	this->transitionRefreshInterval = transitionRefreshInterval;
	this->transitionTime = transitionTime;
}

RgbLightHandler::~RgbLightHandler()
{
	this->stop();
	delete pwmDriver;
}

void RgbLightHandler::start()
{
	workerThread = thread(&RgbLightHandler::worker, this);
}

void RgbLightHandler::stop()
{
	if (this->isRunning)
	{
		this->isRunning = false;
		
		// Allow the worker thread to clean up if necessary
		if(workerThread.joinable())
			workerThread.join();
	}
}

// Sync with the worker and update the pattern for the left LEDs
// If enabled, will cause a transition into the new pattern to start
void RgbLightHandler::setPatternLeft(RgbLightPattern * pattern)
{
	patternMutex.lock();

	// Remove the previous (cloned) pattern from memory 
	if (this->patternLeft != NULL)
		delete this->patternLeft;

	// We need the lifetime of the pattern to be as long as the thread lives
	// therefore, we create a clone of the object
	this->patternLeft = pattern->clone();

	// Signal we changed patterns and we want a transition
	this->patternsChanged = true;
	patternMutex.unlock();
}

// Sync with the worker and update the pattern for the right LEDs
// If enabled, will cause a transition into the new pattern to start
void RgbLightHandler::setPatternRight(RgbLightPattern * pattern)
{
	patternMutex.lock();

	// Remove the previous (cloned) pattern from memory
	if (this->patternRight != NULL)
		delete this->patternRight;

	// We need the lifetime of the pattern to be as long as the thread lives
	// therefore, we create a clone of the object
	this->patternRight = pattern->clone();

	// Signal we changed patterns and we want a transition
	this->patternsChanged = true;
	patternMutex.unlock();
}

// Sync with the worker and update the patterns for both left and right simultaneously
// If enabled, will cause a transition into the new pattern to start
void RgbLightHandler::setPatterns(RgbLightPattern * pattern)
{
	patternMutex.lock();

	// Remove the previous (cloned) patterns from memory
	if (this->patternLeft != NULL)
		delete this->patternLeft;

	if (this->patternRight != NULL)
		delete this->patternRight;

	// We need the lifetime of the pattern to be as long as the thread lives
	// therefore, we create a clone of the object
	this->patternLeft = pattern->clone();
	this->patternRight = pattern->clone();

	// Signal we changed patterns and we want a transition
	this->patternsChanged = true;

	patternMutex.unlock();
}

// Continuously polls the patterns for the current color to be displayed
// and sends the color to the PWM driver
void RgbLightHandler::worker()
{
	this->isRunning = true;

	int interval = 20;
	vector<float> colorLeft = { 0.0f, 0.0f, 0.0f, 0.0f };
	vector<float> colorRight = { 0.0f, 0.0f, 0.0f, 0.0f };

	// If !isRunning, the thread is requested to finish
	while (this->isRunning)
	{
		patternMutex.lock();

		// Start a transition if the pattern has changed
		if (this->patternsChanged && this->transitionsEnabled)
		{
			this->transitionBoth(colorLeft, colorRight);
			this->patternsChanged = false;
		}

		// The fastest pattern determines the interval
		// TODO: Communicate this back to the pattern, so pattern can sync speed
		// or, use a time variable t
		interval = min(this->patternLeft->refreshInterval, this->patternRight->refreshInterval);

		// Get the current colors to be displayed
		colorLeft = this->patternLeft->getColor();
		colorRight = this->patternRight->getColor();

		patternMutex.unlock();

		// Send the colors to the PWM driver
		pwmDriver->setRgbw(colorLeft, colorRight);

		// Wait before we update the color
		this_thread::sleep_for(chrono::milliseconds(interval));
	}
}

// Transition from pattern a to b for both left and right LEDs
void RgbLightHandler::transitionBoth(vector<float> leftFrom, vector<float> rightFrom)
{
	// Find the targets
	vector<float> leftTo = this->patternLeft->getColor(); 
	vector<float> rightTo = this->patternRight->getColor();

	vector<float> leftDelta = { 0.0f, 0.0f, 0.0f, 0.0f };
	vector<float> rightDelta = { 0.0f, 0.0f, 0.0f, 0.0f };

	// Use a float here, so we only have to cast once
	float maxi = this->transitionTime / (float)this->transitionRefreshInterval;

	// Prevent a divide by 0 and skip the transition
	if (maxi == 0.0)
		return;

	// Calculate the color delta per step for both left and right
	for (int j = 0; j < NUM_COLORS; j++)
	{
		leftDelta[j] = (leftTo[j] - leftFrom[j]) / maxi;
		rightDelta[j] = (rightTo[j] - rightFrom[j]) / maxi;
	}

	// Update the displayed colors until we reach the target color
	for (int i = 0; i < maxi; i++)
	{
		// Calculate the current colors
		for (int j = 0; j < NUM_COLORS; j++)
		{
			leftFrom[j] = max(min(leftFrom[j] + leftDelta[j], 1.0f), 0.0f);
			rightFrom[j] = max(min(rightFrom[j] + rightDelta[j], 1.0f), 0.0f);
		}

		pwmDriver->setRgbw(leftFrom, rightFrom);
		this_thread::sleep_for(chrono::milliseconds(this->transitionRefreshInterval));
	}
}
