<?hh // strict
/**********************************************************\
|                                                          |
|                          hprose                          |
|                                                          |
| Official WebSite: http://www.hprose.com/                 |
|                   http://www.hprose.org/                 |
|                                                          |
\**********************************************************/

/**********************************************************\
 *                                                        *
 * Hprose/HttpClient.hh                                   *
 *                                                        *
 * hprose http client library for hack.                   *
 *                                                        *
 * LastModified: Feb 27, 2015                             *
 * Author: Ma Bingyao <andot@hprose.com>                  *
 *                                                        *
\**********************************************************/

namespace Hprose {
    class HttpClient extends Client {
        private static Map<string, Map<string, Map<string, string>>> $cookieManager = Map {};
        private string $host = '';
        private string $path = '';
        private bool $secure = false;
        private string $proxy = '';
        private int $timeout = 30000;
        private bool $keepAlive = true;
        private int $keepAliveTimeout = 300;
        private Map<string, string> $header;
        private resource $curl;
        public static function keepSession(): void {
            // UNSAFE
            if (array_key_exists('HPROSE_COOKIE_MANAGER', $_SESSION)) {
                self::$cookieManager = $_SESSION['HPROSE_COOKIE_MANAGER'];
            }
            register_shutdown_function(() ==> {
                $_SESSION['HPROSE_COOKIE_MANAGER'] = self::$cookieManager;
            });
        }
        private function setCookie(array<string> $headers): void {
            foreach ($headers as $header) {
                @list($name, $value) = explode(':', $header, 2);
                if (strtolower($name) == 'set-cookie' ||
                    strtolower($name) == 'set-cookie2') {
                    $cookies = explode(';', trim($value));
                    $cookie = Map {};
                    $pair = explode('=', trim($cookies[0]), 2);
                    $cookie['name'] = $pair[0];
                    if (count($pair) > 1) $cookie['value'] = $pair[1];
                    for ($i = 1; $i < count($cookies); $i++) {
                        $pair = explode('=', trim($cookies[$i]), 2);
                        $cookie[strtoupper($pair[0])] = (count($pair) > 1) ? $pair[1] : '';
                    }
                    // Tomcat can return SetCookie2 with path wrapped in "
                    if (array_key_exists('PATH', $cookie)) {
                        $cookie['PATH'] = trim($cookie['PATH'], '"');
                    }
                    else {
                        $cookie['PATH'] = '/';
                    }
                    if (array_key_exists('DOMAIN', $cookie)) {
                        $cookie['DOMAIN'] = strtolower($cookie['DOMAIN']);
                    }
                    else {
                        $cookie['DOMAIN'] = $this->host;
                    }
                    if (!array_key_exists($cookie['DOMAIN'], self::$cookieManager)) {
                        self::$cookieManager[$cookie['DOMAIN']] = Map {};
                    }
                    self::$cookieManager[$cookie['DOMAIN']][$cookie['name']] = $cookie;
                }
            }
        }
        private function getCookie(): string {
            $cookies = array();
            foreach (self::$cookieManager as $domain => $cookieList) {
                if (strpos($this->host, $domain) !== false) {
                    $names = array();
                    foreach ($cookieList as $cookie) {
                        if (array_key_exists('EXPIRES', $cookie) && (time() > strtotime($cookie['EXPIRES']))) {
                            $names[] = $cookie['name'];
                        }
                        elseif (strpos($this->path, $cookie['PATH']) === 0) {
                            if ((($this->secure &&
                                 array_key_exists('SECURE', $cookie)) ||
                                 !array_key_exists('SECURE', $cookie)) &&
                                  array_key_exists('value', $cookie)) {
                                $cookies[] = $cookie['name'] . '=' . $cookie['value'];
                            }
                        }
                    }
                    foreach ($names as $name) {
                        self::$cookieManager[$domain]->remove($name);
                    }
                }
            }
            if (count($cookies) > 0) {
                return "Cookie: " . implode('; ', $cookies);
            }
            return '';
        }
        private function init_url(string $url): void {
            if ($url) {
                $url = parse_url($url);
                $this->secure = (strtolower($url['scheme']) == 'https');
                $this->host = strtolower($url['host']);
                $this->path = array_key_exists('path', $url) ? $url['path'] : "/";
                $this->timeout = 30000;
                $this->keepAlive = true;
                $this->keepAliveTimeout = 300;
            }
        }
        public function __construct(string $url = '') {
            parent::__construct($url);
            $this->init_url($url);
            $this->header = Map {'Content-type' => 'application/hprose'};
            $this->curl = curl_init();
        }
        public function useService(string $url = '', string $namespace = ''): mixed {
            $this->init_url($url);
            return parent::useService($url, $namespace);
        }
        protected function sendAndReceive(string $request): string {
            curl_setopt($this->curl, CURLOPT_URL, $this->url);
            curl_setopt($this->curl, CURLOPT_HEADER, true);
            curl_setopt($this->curl, CURLOPT_SSL_VERIFYPEER, false);
            curl_setopt($this->curl, CURLOPT_RETURNTRANSFER, true);
            curl_setopt($this->curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
            if (!ini_get('safe_mode')) {
                curl_setopt($this->curl, CURLOPT_FOLLOWLOCATION, true);
            }
            curl_setopt($this->curl, CURLOPT_POST, true);
            curl_setopt($this->curl, CURLOPT_POSTFIELDS, $request);
            $headers_array = array($this->getCookie(),
                                    "Content-Length: " . strlen($request));
            if ($this->keepAlive) {
                $headers_array[] = "Connection: keep-alive";
                $headers_array[] = "Keep-Alive: " . $this->keepAliveTimeout;
            }
            else {
                $headers_array[] = "Connection: close";
            }
            foreach ($this->header as $name => $value) {
                $headers_array[] = $name . ": " . $value;
            }
            curl_setopt($this->curl, CURLOPT_HTTPHEADER, $headers_array);
            if ($this->proxy) {
                curl_setopt($this->curl, CURLOPT_PROXY, $this->proxy);
            }
            if (defined('CURLOPT_TIMEOUT_MS')) {
                curl_setopt($this->curl, CURLOPT_TIMEOUT_MS, $this->timeout);
            }
            else {
                curl_setopt($this->curl, CURLOPT_TIMEOUT, $this->timeout / 1000);
            }
            $response = curl_exec($this->curl);
            $errno = curl_errno($this->curl);
            if ($errno) {
                throw new \Exception($errno . ": " . curl_error($this->curl));
            }
            do {
                list($response_headers, $response) = explode("\r\n\r\n", $response, 2);
                $http_response_header = explode("\r\n", $response_headers);
                $http_response_firstline = array_shift($http_response_header);
                $matches = array();
                if (preg_match('@^HTTP/[0-9]\.[0-9]\s([0-9]{3})\s(.*)@',
                               $http_response_firstline, $matches)) {
                    $response_code = $matches[1];
                    $response_status = trim($matches[2]);
                }
                else {
                    $response_code = "500";
                    $response_status = "Unknown Error.";
                }
            } while (substr($response_code, 0, 1) == "1");
            if ($response_code != '200') {
                throw new \Exception($response_code . ": " . $response_status . "\r\n\r\n" . $response);
            }
            $this->setCookie($http_response_header);
            return $response;
        }
        public function setHeader(string $name, string $value): void {
            $lname = strtolower($name);
            if ($lname != 'content-type' &&
                $lname != 'content-length' &&
                $lname != 'host') {
                if ($value) {
                    $this->header[$name] = $value;
                }
                else {
                    $this->header->remove($name);
                }
            }
        }
        public function setProxy(string $proxy = ''): void {
            $this->proxy = $proxy;
        }
        public function setTimeout(int $timeout): void {
            $this->timeout = $timeout;
        }
        public function getTimeout(): int {
            return $this->timeout;
        }
        public function setKeepAlive(bool $keepAlive = true): void {
            $this->keepAlive = $keepAlive;
        }
        public function getKeepAlive(): bool {
            return $this->keepAlive;
        }
        public function setKeepAliveTimeout(int $timeout): void {
            $this->keepAliveTimeout = $timeout;
        }
        public function getKeepAliveTimeout(): int {
            return $this->keepAliveTimeout;
        }
        public function __destruct(): void {
            curl_close($this->curl);
        }
    }
}
