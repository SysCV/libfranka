#!groovy

node {
  step([$class: 'StashNotifier'])

  try {
    checkout scm

    docker.build('libfranka-ci-worker', '.ci').inside {
      stage('Build (Debug)') {
        sh '.ci/debug.sh'
        junit 'build-debug/test_results/*.xml'
      }

      stage('Build (Release)') {
        sh '.ci/release.sh'
        dir('build-release') {
          dir('doc') {
            sh 'tar cfz ../libfranka-docs.tar.gz html'
          }
          archive '*.deb, *.tar.gz'
          publishHTML([allowMissing: false,
                       alwaysLinkToLastBuild: false,
                       keepAll: true,
                       reportDir: 'doc/html',
                       reportFiles: 'index.html',
                       reportName: 'API Documentation'])
        }
      }

      stage('Build (Coverage)') {
        sh '.ci/coverage.sh'
        publishHTML([allowMissing: false,
                     alwaysLinkToLastBuild: false,
                     keepAll: true,
                     reportDir: 'build-coverage/coverage',
                     reportFiles: 'index.html',
                     reportName: 'Code Coverage'])
      }

      stage('Lint') {
        sh '.ci/lint.sh'
      }
    }

    currentBuild.result = 'SUCCESS'
  } catch (e) {
    currentBuild.result = 'FAILED'
    throw e;
  } finally {
    step([$class: 'StashNotifier'])
  }
}
