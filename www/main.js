const galaxy = document.getElementById('galaxy');

function generateStars() {
	const numStars = 1000;

	for (let i = 0; i < numStars; i++) {
		const star = document.createElement('span');
		star.className = 'star';
		star.style.top = `${Math.random() * 100}%`;
		star.style.left = `${Math.random() * 100}%`;
		star.style.animationDuration = `${Math.random() * 2 + 0.5}s`;

		galaxy.appendChild(star);
	}
}

generateStars();
